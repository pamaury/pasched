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
    }

    void dump_schedule(exp_state& st, const std::vector< unit_idx_t >& s, const std::string& prefix)
    {
        for(size_t i = 0; i < s.size(); i++)
            debug() << prefix << st.dag->get_units()[s[i]]->to_string() << "\n";
    }

    void do_schedule(exp_state& st)
    {
        /* timer */
        if(exp_ire(st))
            throw exp_timeout();
        /* early stop */
        if(st.has_schedule && st.cur_rp >= st.best_rp)
            return;
        /* base case: no more schedulable units */
        if(st.schedulable.size() == 0)
        {
            assert(st.live_regs.size() == 0 && "Variables still alive at end of schedule !");
            /* we have a schedule: cool ! */
            if(st.verbose && false)
            {
                debug() << "New schedule (RP=" << st.cur_rp << "):\n";
                dump_schedule(st, st.cur_schedule, "  ");
            }

            /* ignore poor schedule */
            if(st.has_schedule && st.cur_rp >= st.best_rp)
                return;

            st.has_schedule = true;
            st.best_rp = st.cur_rp;
            st.best_schedule = st.cur_schedule;

            return;
        }

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
            st.cur_rp = std::max(st.cur_rp, st.live_regs.size() + st.unit_sinfo[unit].irp);

            /* create regs */
            for(size_t j = 0; j < st.unit_sinfo[unit].reg_create.size(); j++)
            {
                schedule_dep::reg_t reg = st.unit_sinfo[unit].reg_create[j];
                assert(st.live_regs.find(reg) == st.live_regs.end() && "Created variable is already alive !");

                st.live_regs[reg].id = reg;
                st.live_regs[reg].nb_use_left = st.unit_sinfo[unit].reg_create_use_count[j];
            }

            /* compute RP */
            st.cur_rp = std::max(st.cur_rp, st.live_regs.size());

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

            /* schedule */
            do_schedule(st);

            /* restore everything */
            st.cur_rp = old_rp;
            st.cur_schedule.pop_back();
            st.live_regs = old_live;
            st.schedulable = old_schedulable;
            st.unit_dinfo = old_dinfo;
        }
    }

    void exp_schedule(exp_state& st)
    {
        init_state(st);
        try
        {
            do_schedule(st);

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

        if(true)
        {
            std::cout << "RP=" << st.best_rp << "\n";
            generic_schedule_chain gsc;
            for(size_t i = 0; i < st.best_schedule.size(); i++)
                gsc.append_unit(dag.get_units()[st.best_schedule[i]]);
            std::cout << "  vs RP=" << gsc.compute_rp_against_dag(dag) << "\n";
        }
    }
    else
    {
        /* fallback */
        m_fallback_sched->schedule(dag, sc);
    }
}

}