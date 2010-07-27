#include "scheduler.hpp"
#include "tools.hpp"
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <set>
#include <queue>
#include <iostream>
#include <cassert>
#include <ctime>
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <string.h>

namespace PAMAURY_SCHEDULER_NS
{

exp_scheduler::exp_scheduler(const scheduler *fallback_sched, size_t fallback_timeout, bool verbose)
    :m_fallback_sched(fallback_sched), m_timeout(fallback_timeout), m_verbose(verbose)
{
}

exp_scheduler::~exp_scheduler()
{
}

namespace
{
    struct bitmap
    {
        bitmap()
        {
        }
        
        bitmap(size_t nb_bits)
        {
            set_nb_bits(nb_bits);
        }

        void set_nb_bits(size_t nb_bits)
        {
            m_nb_bits = nb_bits;
            m_nb_chunks = (nb_bits + BITS_PER_CHUNKS - 1) / BITS_PER_CHUNKS;
            clear();
        }

        bool operator<(const bitmap& o) const
        {
            assert(o.m_nb_bits == m_nb_bits);

            for(int i = m_nb_chunks - 1; i >= 0; i--)
            {
                if(m_chunks[i] < o.m_chunks[i])
                    return true;
                else if(m_chunks[i] > o.m_chunks[i])
                    return false;
            }

            return false;
        }

        void set_bit(size_t b)
        {
            m_chunks[b / BITS_PER_CHUNKS] |= (uintmax_t)1 << (b % BITS_PER_CHUNKS);
        }

        void clear_bit(size_t b)
        {
            m_chunks[b / BITS_PER_CHUNKS] &= ~((uintmax_t)1 << (b % BITS_PER_CHUNKS));
        }

        void clear()
        {
            memset(m_chunks, 0, sizeof m_chunks);
        }

        void set()
        {
            clear();
            complement();
        }

        static size_t nbs(uintmax_t n)
        {
            size_t cnt = 0;
            while(n != 0)
            {
                cnt += n % 2;
                n /= 2;
            }
            return cnt;
        }

        size_t nb_bits_set() const
        {
            size_t cnt = 0;
            for(size_t i = 0; i < m_nb_chunks; i++)
                cnt += nbs(m_chunks[i]);
            return cnt;
        }

        size_t nb_bits_cleared() const
        {
            return m_nb_bits - nb_bits_set();
        }

        void complement()
        {
            for(size_t i = 0; (i + 1) < m_nb_chunks; i++)
                m_chunks[i] = ~m_chunks[i];
            m_chunks[m_nb_chunks - 1] = (~m_chunks[m_nb_chunks - 1]) & ((1 << (m_nb_chunks % BITS_PER_CHUNKS)) - 1);
        }

        bitmap& operator|=(const bitmap& o)
        {
            assert(o.m_nb_bits == m_nb_bits);
            
            for(size_t i = 0; i < m_nb_chunks; i++)
                m_chunks[i] |= o.m_chunks[i];
        }

        bitmap& operator&=(const bitmap& o)
        {
            assert(o.m_nb_bits == m_nb_bits);
            
            for(size_t i = 0; i < m_nb_chunks; i++)
                m_chunks[i] &= o.m_chunks[i];
        }

        bool operator==(const bitmap& o) const
        {
            assert(o.m_nb_bits == m_nb_bits);
            
            for(size_t i = 0; i < m_nb_chunks; i++)
                if(m_chunks[i] != o.m_chunks[i])
                    return false;
            return true;
        }

        bool operator!=(const bitmap& o) const
        {
            return !operator==(o);
        }

        static const size_t MAX_CHUNKS = 10; /* should not be a limit in practise */
        static const size_t BITS_PER_CHUNKS = sizeof(uintmax_t) * 8;
        uintmax_t m_chunks[MAX_CHUNKS];
        size_t m_nb_chunks;
        size_t m_nb_bits;
    };
    
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
        /* list of created registers */
        std::vector< schedule_dep::reg_t > reg_create;
        /* number of uses of each created variable */
        std::vector< size_t > reg_create_use_count;
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

    struct exp_result
    {
        exp_result(bool found, size_t rp = SIZE_MAX)
            :found_schedule(found), achieved_rp(rp) {}
        bool found_schedule;
        /* the RP only consider the subgraph to schedule
         * and is not a global result */
        size_t achieved_rp;
    };

    struct exp_cache_result
    {
        exp_cache_result()
            :res(false) {}
        exp_cache_result(exp_result r, unit_idx_t best)
            :res(r), best_unit(best) {}
        exp_result res;
        unit_idx_t best_unit;
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
            /* reg_create */
            rset = dag.get_reg_create(unit);
            st.unit_sinfo[u].reg_create.reserve(rset.size());
            for(rit = rset.begin(); rit != rset.end(); ++rit)
                st.unit_sinfo[u].reg_create.push_back(*rit);
            /* reg_create_use_count */
            st.unit_sinfo[u].reg_create_use_count.resize(rset.size());
            for(size_t i = 0; i < st.unit_sinfo[u].reg_create.size(); i++)
                for(size_t j = 0; j < dag.get_succs(unit).size(); j++)
                {
                    const schedule_dep& dep = dag.get_succs(unit)[j];
                    if(dep.kind() == schedule_dep::data_dep && dep.reg() == st.unit_sinfo[u].reg_create[i])
                        st.unit_sinfo[u].reg_create_use_count[i]++;
                }
            /* irp */
            st.unit_sinfo[u].irp = unit->internal_register_pressure();
        }
    }

    inline void start_timer(exp_state& st)
    {
        if(st.timeout == 0)
            return;
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

    void dump_schedule(exp_state& st, const std::vector< unit_idx_t >& s, const std::string& prefix)
    {
        for(size_t i = 0; i < s.size(); i++)
            debug() << prefix << st.dag->get_units()[s[i]]->to_string() << "\n";
    }

    exp_result do_schedule(exp_state& st)
    {
        if(st.cache_mem.find(st.cache_bm) != st.cache_mem.end())
        {
            //std::cout << "size=" << st.cache_mem.size() << "(" << st.cache_bm.nb_bits_set() << ")\n";
            exp_cache_result& cc = st.cache_mem[st.cache_bm];
            /* no schedule that worth it ? */
            if(!cc.res.found_schedule)
                return exp_result(false);
            /* see if it would produce a best result */
            size_t new_rp = std::max(st.cur_rp, cc.res.achieved_rp);
            if(st.has_schedule && new_rp >= st.best_rp)
                /* no */
                return exp_result(false);
            /* yes, rebuild schedule from cache */
            bitmap bm(st.cache_bm);
            std::vector< unit_idx_t > sched = st.cur_schedule;
            while(st.cache_mem.find(bm) != st.cache_mem.end())
            {
                unit_idx_t unit = st.cache_mem[bm].best_unit;
                sched.push_back(unit);
                bm.set_bit(unit);
            }
            /* sanity checks */
            assert(sched.size() == st.nb_units);

            //std::cout << "new RP=" << new_rp << "\n";
            st.best_rp = new_rp;
            st.best_schedule = sched;

            return cc.res;
        }
        /* timer */
        if(exp_ire(st))
            throw exp_timeout();
        /* early stop */
        if(st.has_schedule && st.cur_rp >= st.best_rp)
            return exp_result(false); /* did not found a schedule */
        /* base case: no more schedulable units */
        if(st.schedulable.size() == 0)
        {
            assert(st.live_regs.size() == 0 && "Variables still alive at end of schedule !");
            /* we have a schedule: cool ! */
            if(st.verbose)
            {
                debug() << "New schedule (RP=" << st.cur_rp << "):\n";
                dump_schedule(st, st.cur_schedule, "  ");
            }

            /* ignore poor schedule */
            if(!st.has_schedule || st.cur_rp < st.best_rp)
            {
                //std::cout << "new RP=" << st.cur_rp << "\n";
                st.has_schedule = true;
                st.best_rp = st.cur_rp;
                st.best_schedule = st.cur_schedule;
            }

            /* RP is 0 here */
            return exp_result(st.has_schedule, 0);
        }

        exp_result res(false);
        size_t best_unit = SIZE_MAX;
        /* try each schedulable unit */
        for(size_t i = 0; i < st.schedulable.size(); i++)
        {
            /* save live regs */
            std::map< schedule_dep::reg_t, exp_live_reg > old_live = st.live_regs;
            /* save schedulable */
            std::vector< unit_idx_t > old_schedulable = st.schedulable;
            /* save dynamic info */
            std::vector< exp_dynamic_unit_info > old_dinfo = st.unit_dinfo;
            /* save RP */
            size_t old_rp = st.cur_rp;

            /* remove unit from schedulables */
            size_t unit = st.schedulable[i];
            unordered_vector_remove(i, st.schedulable);
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
            for(size_t j = 0; j < st.unit_sinfo[unit].reg_create.size(); j++)
            {
                schedule_dep::reg_t reg = st.unit_sinfo[unit].reg_create[j];
                assert(st.live_regs.find(reg) == st.live_regs.end() && "Created variable is already alive !");

                st.live_regs[reg].id = reg;
                st.live_regs[reg].nb_use_left = st.unit_sinfo[unit].reg_create_use_count[j];
            }

            /* compute RP */
            inst_rp = std::max(inst_rp, st.live_regs.size());
            st.cur_rp = std::max(st.cur_rp, inst_rp);

            /* update deps and release units */
            for(size_t i = 0; i < st.unit_sinfo[unit].unit_release.size(); i++)
            {
                unit_idx_t rel = st.unit_sinfo[unit].unit_release[i];
                assert(st.unit_dinfo[rel].nb_dep_left > 0 && "Released unit has no dependencies left !");

                st.unit_dinfo[rel].nb_dep_left--;
                /* Free ? */
                if(st.unit_dinfo[rel].nb_dep_left == 0)
                    st.schedulable.push_back(rel);
            }

            st.cache_bm.set_bit(unit);
            /* schedule */
            exp_result tmp = do_schedule(st);
            st.cache_bm.clear_bit(unit);

            if(tmp.found_schedule)
            {
                res.found_schedule = true;
                tmp.achieved_rp = std::max(tmp.achieved_rp, inst_rp);
                if(tmp.achieved_rp < res.achieved_rp)
                {
                    res.achieved_rp = tmp.achieved_rp;
                    best_unit = unit;
                }
            }

            /* restore everything */
            st.cur_rp = old_rp;
            st.cur_schedule.pop_back();
            st.live_regs = old_live;
            st.schedulable = old_schedulable;
            st.unit_dinfo = old_dinfo;
        }

        st.cache_mem[st.cache_bm] = exp_cache_result(res, best_unit);

        return res;
    }

    void exp_schedule(exp_state& st)
    {
        init_state(st);
        try
        {
            exp_result res = do_schedule(st);

            if(st.verbose && st.has_schedule)
            {
                debug() << "Best schedule(RP=" << st.best_rp << "):\n";
                dump_schedule(st, st.best_schedule, "  ");
            }

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

    compute_static_info(dag, st);
    exp_schedule(st);

    if(st.status == status_success ||
            (st.status == status_timeout && st.has_schedule))
    {
        assert(st.has_schedule && "Success but not valid schedule ?!");
        assert(st.best_schedule.size() == st.nb_units && "Schedule has the wrong size !");
        for(size_t i = 0; i < st.best_schedule.size(); i++)
            sc.append_unit(dag.get_units()[st.best_schedule[i]]);

        #ifdef ENABLE_SCHED_AUTO_CHECK_RP
        {
            generic_schedule_chain gsc;
            for(size_t i = 0; i < st.best_schedule.size(); i++)
                gsc.append_unit(dag.get_units()[st.best_schedule[i]]);

            assert(st.best_rp == gsc.compute_rp_against_dag(dag) && "Mismatch between announced and actual RP in exp_scheduler");
        }
        #endif
    }
    else
    {
        /* fallback */
        m_fallback_sched->schedule(dag, sc);
    }
}

}
