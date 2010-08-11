#include "scheduler.hpp"
#include "sched-dag-viewer.hpp"
#include "tools.hpp"
#include "adt.hpp"
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <set>
#include <queue>
#include <iostream>
#include <cassert>
#include <ctime>

namespace PAMAURY_SCHEDULER_NS
{

STM_DECLARE(exp_scheduler)

exp_scheduler::exp_scheduler(const scheduler *fallback_sched, size_t fallback_timeout, bool verbose)
    :m_fallback_sched(fallback_sched), m_timeout(fallback_timeout), m_verbose(verbose)
{
}

exp_scheduler::~exp_scheduler()
{
}

namespace
{
    typedef unsigned short unit_idx_t;

    struct exp_live_reg
    {
        schedule_dep::reg_t id;
        /* number of uses of the register still to be scheduled before the register dies */
        size_t nb_use_left;
    };

    struct exp_static_unit_info
    {
        /* list of used registers */
        std::vector< schedule_dep::reg_t > reg_use;
        /* list of created phys registers */
        std::vector< schedule_dep::reg_t > reg_phys_create;
        /* list of all created registers */
        std::vector< schedule_dep::reg_t > reg_all_create;
        /* number of uses of each created variable */
        std::vector< size_t > reg_all_create_use_count;
        /* list of predecessors */
        std::vector< unit_idx_t > unit_depend;
        /* list of successors */
        std::vector< unit_idx_t > unit_release;
        /* internal register pressure */
        size_t irp;
    };

    struct exp_dynamic_unit_info
    {
        size_t nb_dep_left;
    };

    enum exp_status
    {
        status_success,
        status_timeout
    };

    struct exp_cache_result
    {
        exp_cache_result()
        {
            fw.valid = false;
            bw.valid = false;
        }

        /* NOTE: forward and backward parts are compeltely independent */
        struct
        {
            /* does it contain valid forward info ? */
            bool valid;
            /* best achieved RP for the scheduled graph (forward view) */
            size_t achieved_rp;
        }fw;
        
        struct
        {
            /* does it contain valid backward info ? */
            bool valid;
            /* is the cached result optimal ? */
            bool optimal;
            /* best achieved RP */
            size_t achieved_rp;
            /* best unit to pick */
            size_t best_unit;
        }bw;
    };

    struct exp_state
    {
        /* global parameters */
        const schedule_dag *dag;
        size_t timeout;
        bool verbose;
        /* for timeout */
        clock_t clock_start;
        size_t clock_div;
        size_t clock_cycle;
        /* [unit_idx_t -> unit] map is dag.get_units() */
        /* [unit -> unit_idx_t] map follows */
        std::map< const schedule_unit *, unit_idx_t > unit_idx_map;
        /* static info used during scheduling */
        size_t nb_units;
        std::vector< exp_static_unit_info> unit_sinfo; /* index by unit_idx_t */
        /* dynamic info used during scheduling */
        std::map< schedule_dep::reg_t, exp_live_reg > live_regs;
        std::vector< unit_idx_t > schedulable;
        std::vector< exp_dynamic_unit_info > unit_dinfo;
        size_t cur_rp;
        std::vector< unit_idx_t > cur_schedule;
        /* global results */
        bool has_schedule;
        size_t best_rp; /* if has_schedule */
        std::vector< unit_idx_t > best_schedule; /* if has_schedule */
        bool proven_optimal; /* if has_schedule */
        exp_status status;

        /* cache */
        std::map< bitmap, exp_cache_result > cache_mem;
        bitmap cache_bm;
    };

    struct exp_timeout
    {
    };

    void compute_static_info(const schedule_dag& dag, exp_state& st)
    {
        st.dag = &dag;
        /* unit_idx_map */
        for(size_t u = 0; u < dag.get_units().size(); u++)
            st.unit_idx_map[dag.get_units()[u]] = (unit_idx_t)u;
        /* nb_units */
        st.nb_units = dag.get_units().size();
        /* unit_sinfo */
        st.unit_sinfo.resize(st.nb_units);
        for(unit_idx_t u = 0; u < st.nb_units; u++)
        {
            const schedule_unit *unit = dag.get_units()[u];
            std::set< const schedule_unit * > set;
            std::set< schedule_dep::reg_t > rset;
            std::set< const schedule_unit * >::iterator it;
            std::set< schedule_dep::reg_t >::iterator rit;
            /* unit_depend */
            set = dag.get_reachable(unit, schedule_dag::rf_follow_preds | schedule_dag::rf_immediate);
            st.unit_sinfo[u].unit_depend.reserve(set.size());
            for(it = set.begin(); it != set.end(); ++it)
                st.unit_sinfo[u].unit_depend.push_back(st.unit_idx_map[*it]);
            /* unit_release */
            set = dag.get_reachable(unit, schedule_dag::rf_follow_succs | schedule_dag::rf_immediate);
            st.unit_sinfo[u].unit_release.reserve(set.size());
            for(it = set.begin(); it != set.end(); ++it)
                st.unit_sinfo[u].unit_release.push_back(st.unit_idx_map[*it]);
            /* reg_use */
            rset = dag.get_reg_use(unit);
            st.unit_sinfo[u].reg_use.reserve(rset.size());
            for(rit = rset.begin(); rit != rset.end(); ++rit)
                st.unit_sinfo[u].reg_use.push_back(*rit);
            /* reg_phys_create */
            rset = dag.get_reg_phys_create(unit);
            st.unit_sinfo[u].reg_phys_create.reserve(rset.size());
            for(rit = rset.begin(); rit != rset.end(); ++rit)
                st.unit_sinfo[u].reg_phys_create.push_back(*rit);
            /* reg_all_create */
            rset = dag.get_reg_create(unit);
            st.unit_sinfo[u].reg_all_create.reserve(rset.size());
            for(rit = rset.begin(); rit != rset.end(); ++rit)
                st.unit_sinfo[u].reg_all_create.push_back(*rit);
            /* reg_all_create_use_count */
            st.unit_sinfo[u].reg_all_create_use_count.resize(rset.size());
            for(size_t i = 0; i < st.unit_sinfo[u].reg_all_create.size(); i++)
                for(size_t j = 0; j < dag.get_succs(unit).size(); j++)
                {
                    const schedule_dep& dep = dag.get_succs(unit)[j];
                    if(dep.is_data() && dep.reg() == st.unit_sinfo[u].reg_all_create[i])
                        st.unit_sinfo[u].reg_all_create_use_count[i]++;
                }
            /* irp */
            st.unit_sinfo[u].irp = unit->internal_register_pressure();
        }
    }

    inline void start_timer(exp_state& st)
    {
        /* set the diviser to 100, this will cause the expire check to call clock
         * only once in 100 times, to avoid too much overhead */
        st.clock_div = 100;
        st.clock_cycle = st.clock_div;
        st.clock_start = clock();
    }

    inline bool exp_ire(exp_state& st)
    {
        if(st.timeout == 0)
            return false;
        st.clock_cycle--;
        if(st.clock_cycle == 0)
        {
            st.clock_cycle = st.clock_div;
            clock_t c = clock();
            if((c - st.clock_start) >= (long)((CLOCKS_PER_SEC / 1000) * st.timeout))
                return true;
        }
        
        return false;
    }

    void init_state(exp_state& st)
    {
        /* set global state */
        st.has_schedule = false;
        st.cur_rp = 0;
        /* compute root nodes */
        for(size_t i = 0; i < st.nb_units; i++)
            if(st.unit_sinfo[i].unit_depend.size() == 0)
                st.schedulable.push_back(i);
        /* each nodes still depends on all its predecessors */
        st.unit_dinfo.resize(st.nb_units);
        for(size_t i = 0; i < st.nb_units; i++)
            st.unit_dinfo[i].nb_dep_left = st.unit_sinfo[i].unit_depend.size();
        /* start timer */
        start_timer(st);

        st.cache_bm.set_nb_bits(st.nb_units);
    }

    void do_schedule(exp_state& st)
    {
        assert(st.cache_bm.nb_bits_set() == st.cur_schedule.size() && "Inconsistent bitmap");
        #ifdef ENABLE_EXPENSIVE_SCHED_AUTO_CHECK_RP
        {
            generic_schedule_chain chain;
            for(size_t i = 0; i < st.cur_schedule.size(); i++)
                chain.append_unit(st.dag->get_units()[st.cur_schedule[i]]);
            if(chain.compute_rp_against_dag(*st.dag, false) != st.cur_rp)
            {
                std::cout << "claimed: " << st.cur_rp << "\n";
                std::cout << "actual " << chain.compute_rp_against_dag(*st.dag, false) << "\n";
                assert(false);
            }
        }
        #endif
        /* timer */
        if(exp_ire(st))
            throw exp_timeout();
        /* cache entry */
        exp_cache_result& cc = st.cache_mem[st.cache_bm];
        /* if we are in the same situation as before but without a best RP, stop now */
        if(cc.fw.valid && cc.fw.achieved_rp <= st.cur_rp)
            return;
        cc.fw.valid = true;
        cc.fw.achieved_rp = st.cur_rp;
        /* if the current RP is already higher than the best, stop now */
        if(st.has_schedule && st.cur_rp >= st.best_rp)
            return;
        /* do we have some cached result ? */
        if(cc.bw.valid)
        {
            /* see if it would produce a best result */
            size_t new_rp = std::max(st.cur_rp, cc.bw.achieved_rp);

            if(st.has_schedule && new_rp >= st.best_rp)
            {
                /* no, then if the result is optimal, just stop */
                if(cc.bw.optimal)
                    return;
                /* no, but the backward result is not optimal so let's try to improve it */
                goto Lcompute;
            }
            /* yes, then rebuild a schedule from cache */
            bitmap bm(st.cache_bm);
            std::vector< unit_idx_t > sched = st.cur_schedule;

            /* note: if the result has been cached for st.cache_bm,
             *       then it must have been for all the subsequent subgraph,
             *       so there is not need to check */
            while(sched.size() < st.nb_units)
            {
                assert(st.cache_mem[bm].bw.valid && "Cached result with non-cached best path ?");
                unit_idx_t unit = st.cache_mem[bm].bw.best_unit;
                sched.push_back(unit);
                bm.set_bit(unit);
            }
            /* sanity checks */
            #ifdef ENABLE_SCHED_AUTO_CHECK_RP
            generic_schedule_chain chain;
            for(size_t i = 0; i < sched.size(); i++)
                chain.append_unit(st.dag->get_units()[sched[i]]);
            assert(sched.size() == st.nb_units);
            if(chain.compute_rp_against_dag(*st.dag) != new_rp)
            {
                std::cout << "claimed: " << new_rp << "\n";
                std::cout << "actual " << chain.compute_rp_against_dag(*st.dag) << "\n";
                assert(false);
            }
            #endif

            //std::cout << "cc new RP=" << new_rp << "\n";

            /* update best */
            st.best_rp = new_rp;
            st.best_schedule = sched;
            
            /* stop */
            return;
        }
        /* either we have no cached result or not good enough cache, so let's compute something */
        Lcompute:
        
        /* base case: no more schedulable units */
        if(st.schedulable.size() == 0)
        {
            assert(st.live_regs.size() == 0 && "Variables still alive at end of schedule !");

            /* if we are there, we should always improve */
            assert((!st.has_schedule || st.cur_rp < st.best_rp) && "Why the hell are we there if we do not improve best ?");
            
            //std::cout << "new RP=" << st.cur_rp << "\n";
            /* update best */
            st.has_schedule = true;
            st.best_rp = st.cur_rp;
            st.best_schedule = st.cur_schedule;

            #ifdef ENABLE_SCHED_AUTO_CHECK_RP
            {
                assert(st.best_schedule.size() == st.nb_units);
                generic_schedule_chain chain;
                for(size_t i = 0; i < st.best_schedule.size(); i++)
                    chain.append_unit(st.dag->get_units()[st.best_schedule[i]]);
                assert(chain.get_unit_count() == st.nb_units);
                if(chain.compute_rp_against_dag(*st.dag) != st.best_rp)
                {
                    std::cout << "claimed: " << st.best_rp << "\n";
                    std::cout << "actual: " << chain.compute_rp_against_dag(*st.dag) << "\n";
                    assert(false);
                }
            }
            #endif

            /* no cached for leaves */
            return;
        }

        /* normal case: there are things to schedule */
        #define OPTIMIZE_SAVE_RESTORE

        cc.bw.valid = false;
        cc.bw.optimal = true;
        cc.bw.best_unit = SIZE_MAX;
        cc.bw.achieved_rp = SIZE_MAX;
        /* try each schedulable unit */
        for(size_t i = 0; i < st.schedulable.size(); i++)
        {
            #ifndef OPTIMIZE_SAVE_RESTORE
            /* save live regs */
            std::map< schedule_dep::reg_t, exp_live_reg > old_live = st.live_regs;
            /* save schedulable */
            std::vector< unit_idx_t > old_schedulable = st.schedulable;
            /* save dynamic info */
            std::vector< exp_dynamic_unit_info > old_dinfo = st.unit_dinfo;
            #endif
            /* save RP */
            size_t old_rp = st.cur_rp;

            unit_idx_t unit = st.schedulable[i];

            /* Even before trying, check that this unit does not create a physical register that
             * is already alive */
            bool phys_prevent_sched = false;
            for(size_t j = 0; j < st.unit_sinfo[unit].reg_phys_create.size(); j++)
            {
                schedule_dep::reg_t reg = st.unit_sinfo[unit].reg_phys_create[j];
                if(st.live_regs.find(reg) == st.live_regs.end())
                    continue; /* safe, it's not alive */
                /* unsafe, it is alive, so for the unit to be schedulable we must make sure
                 * that it also destroys the register, that is, that this is the last use of
                 * the register */
                if(container_contains(st.unit_sinfo[unit].reg_use, reg) &&
                        st.live_regs[reg].nb_use_left == 1)
                    continue; /* safe */
                /* the unit is not schedulable */
                phys_prevent_sched = true;
            }
            if(phys_prevent_sched)
                continue;
            /* remove unit from schedulables */
            unordered_vector_remove(i, st.schedulable);
            /* and add it to current schedule */
            st.cur_schedule.push_back(unit);

            /* update regs and kill regs */
            for(size_t j = 0; j < st.unit_sinfo[unit].reg_use.size(); j++)
            {
                schedule_dep::reg_t reg = st.unit_sinfo[unit].reg_use[j];
                assert(st.live_regs.find(reg) != st.live_regs.end() && "Used variable is not alive !");
                assert(st.live_regs[reg].id == reg && "Inconsistent reg map !");
                assert(st.live_regs[reg].nb_use_left > 0 && "Variable has more use than expected !");
                
                st.live_regs[reg].nb_use_left--;
                /* dead ? */
                if(st.live_regs[reg].nb_use_left == 0)
                    st.live_regs.erase(reg);
            }

            /* compute RP */
            size_t inst_rp = st.live_regs.size() + st.unit_sinfo[unit].irp;

            /* create regs */
            for(size_t j = 0; j < st.unit_sinfo[unit].reg_all_create.size(); j++)
            {
                schedule_dep::reg_t reg = st.unit_sinfo[unit].reg_all_create[j];
                assert(st.live_regs.find(reg) == st.live_regs.end() && "Created variable is already alive !");

                st.live_regs[reg].id = reg;
                st.live_regs[reg].nb_use_left = st.unit_sinfo[unit].reg_all_create_use_count[j];
            }

            /* compute RP */
            inst_rp = std::max(inst_rp, st.live_regs.size());
            st.cur_rp = std::max(st.cur_rp, inst_rp);

            /* update deps and release units */
            for(size_t j = 0; j < st.unit_sinfo[unit].unit_release.size(); j++)
            {
                unit_idx_t rel = st.unit_sinfo[unit].unit_release[j];
                assert(st.unit_dinfo[rel].nb_dep_left > 0 && "Released unit has no dependencies left !");

                st.unit_dinfo[rel].nb_dep_left--;
                /* Free ? */
                if(st.unit_dinfo[rel].nb_dep_left == 0)
                    st.schedulable.push_back(rel);
            }

            /* mark unit as scheduled */
            st.cache_bm.set_bit(unit);
            /* schedule */
            do_schedule(st);
            /* get caching state */
            const exp_cache_result& rec_cc = st.cache_mem[st.cache_bm];
            /* mark unit as scheduled */
            st.cache_bm.clear_bit(unit);
            /* don't bother if it's not valid */
            if(rec_cc.bw.valid)
            {
                /* now we have something valid to cache */
                cc.bw.valid = true;
                /* optimality is not easy to get */
                cc.bw.optimal = cc.bw.optimal && rec_cc.bw.optimal;
                /* is it better than current ? */
                size_t achieved_rp = std::max(rec_cc.bw.achieved_rp, inst_rp);
                if(achieved_rp < cc.bw.achieved_rp)
                {
                    cc.bw.achieved_rp = achieved_rp;
                    cc.bw.best_unit = unit;
                }
            }

            /* restore everything */
            st.cur_rp = old_rp;
            st.cur_schedule.pop_back();

            #ifdef OPTIMIZE_SAVE_RESTORE
            size_t to_remove = 0;
            /* deupdate deps and unrelease units */
            for(size_t j = 0; j < st.unit_sinfo[unit].unit_release.size(); j++)
            {
                unit_idx_t rel = st.unit_sinfo[unit].unit_release[j];

                if((++st.unit_dinfo[rel].nb_dep_left) == 1)
                    to_remove++;
            }
            /* WARNING: you enter dangerous waters here, this
             *          highly depends on the semantics of
             *          unordered_vector_remove and how st.schedulable
             *          is updated */
            assert(st.schedulable.size() >= to_remove);
            st.schedulable.resize(st.schedulable.size() + 1 - to_remove);
            st.schedulable[st.schedulable.size() - 1] = st.schedulable[i];
            st.schedulable[i] = unit;

            /* uncreate regs */
            for(size_t j = 0; j < st.unit_sinfo[unit].reg_all_create.size(); j++)
            {
                schedule_dep::reg_t reg = st.unit_sinfo[unit].reg_all_create[j];
                assert(st.live_regs.find(reg) != st.live_regs.end() && "Created variable is not alive !");
                assert(st.live_regs[reg].nb_use_left ==  st.unit_sinfo[unit].reg_all_create_use_count[j]
                        && "Created variable has wrong use count !");
                st.live_regs.erase(reg);
            }

            /* deupdate regs and unkill regs */
            for(size_t j = 0; j < st.unit_sinfo[unit].reg_use.size(); j++)
            {
                schedule_dep::reg_t reg = st.unit_sinfo[unit].reg_use[j];
                if((++st.live_regs[reg].nb_use_left) == 1)
                    st.live_regs[reg].id = reg;
            }
            #else
            st.live_regs = old_live;
            st.schedulable = old_schedulable;
            st.unit_dinfo = old_dinfo;
            #endif

            /* At this point, we have an opportunity to stop
             * Indeed, if the current register pressure is already higher than the best,
             * we can stop now and mark the caching as suboptimal. If it happens than we
             * need a better one later on, then we'll continue the computation
             *
             * note: this is just a speedup trick because it will already happen when the recurse
             *       call is done we will avoid lots of computations
             *
             * Of course, don't do it if this is the last schedulable unit ! */
            if(st.cur_rp >= st.best_rp && (i + 1) < st.schedulable.size())
            {
                cc.bw.optimal = false;
                break;
            }
        }
    }

    void exp_schedule(exp_state& st)
    {
        init_state(st);
        try
        {
            do_schedule(st);
            /* status */
            st.status = status_success;
        }
        catch(exp_timeout& et)
        {
            if(st.verbose)
                debug() << "Timeout !\n";
            /* status */
            st.status = status_timeout;
        }
    }
}

void exp_scheduler::schedule(schedule_dag& dag, schedule_chain& sc) const
{
    exp_state st;
    st.timeout = m_timeout;
    st.verbose = m_verbose;

    STM_START(exp_scheduler)
    compute_static_info(dag, st);
    exp_schedule(st);

    if(st.status == status_success ||
            (st.status == status_timeout && st.has_schedule))
    {
        generic_schedule_chain gsc;
        assert(st.has_schedule && "Success but not valid schedule ?!");
        assert(st.best_schedule.size() == st.nb_units && "Schedule has the wrong size !");
        for(size_t i = 0; i < st.best_schedule.size(); i++)
            gsc.append_unit(dag.get_units()[st.best_schedule[i]]);
            
        #ifdef ENABLE_SCHED_AUTO_CHECK_RP
        assert(st.best_rp == gsc.compute_rp_against_dag(dag) && "Mismatch between announced and actual RP in exp_scheduler");
        #endif

        STM_STOP(exp_scheduler)

        if(st.status == status_timeout && false)
        {
            /* Okay, with have a schedule but because of the timeout, it might be of a bad quality,
             * so try to run the fallback scheduler and compare results */
            generic_schedule_chain alt_gsc;
            m_fallback_sched->schedule(dag, alt_gsc);

            size_t exp_rp = gsc.compute_rp_against_dag(dag);
            size_t alt_rp = alt_gsc.compute_rp_against_dag(dag);

            if(alt_rp < exp_rp)
            {
                gsc.clear();
                gsc.insert_units_at(0, alt_gsc.get_units());
                std::cout << "Fallback schedule is better: " << alt_rp << " vs " << exp_rp << "\n";
            }
            else
                std::cout << "Exp (timeout) schedule is better: " << exp_rp << " vs " << alt_rp << "\n";
        }

        assert(gsc.check_against_dag(dag) && "Produced schedule is invalid");
        sc.insert_units_at(sc.get_unit_count(), gsc.get_units());
    }
    else
    {
        STM_STOP(exp_scheduler)
        /* fallback */
        m_fallback_sched->schedule(dag, sc);
    }
}

}
