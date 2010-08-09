#include <scheduler.hpp>
#include <tools.hpp>
#include <sched-dag-viewer.hpp>
#include <climits>
#include <queue>
#include <map>
#include <set>
#include <cassert>

namespace PAMAURY_SCHEDULER_NS
{

/**
 * scheduler
 */

scheduler::scheduler()
{
}

scheduler::~scheduler()
{
}

/**
 * rand_scheduler
 */
STM_DECLARE(rand_scheduler)

rand_scheduler::rand_scheduler()
{
}

rand_scheduler::~rand_scheduler()
{
}

void rand_scheduler::schedule(pasched::schedule_dag& dag, pasched::schedule_chain& c) const
{
    /* do a stupid and inefficient bottom-up scheduling */
    STM_START(rand_scheduler)
    std::map< schedule_dep::reg_t, std::set< const schedule_unit * > > live_reg_uses;
    schedule_dag *cpy = dag.dup();

    while(dag.get_roots().size() > 0)
    {
        const schedule_unit *unit;
        for(size_t i = 0; i < dag.get_roots().size(); i++)
        {
            unit = dag.get_roots()[i];
            for(size_t j = 0; j < dag.get_succs(unit).size(); j++)
            {
                const schedule_dep& dep = dag.get_succs(unit)[j];
                if(!dep.is_phys())
                    continue;
                if(!live_reg_uses[dep.reg()].empty() &&
                        (live_reg_uses[dep.reg()].size() != 1 ||
                            *live_reg_uses[dep.reg()].begin() != unit))
                    goto Lskip;
            }
            break;
            
            Lskip:
            unit = 0;
            continue;
        }

        if(!unit)
        {
            std::vector< dag_printer_opt > opts;
            for(size_t i = 0; i < c.get_unit_count(); i++)
            {
                dag_printer_opt o;
                o.type = dag_printer_opt::po_color_node;
                o.color_node.color = "magenta";
                o.color_node.unit = c.get_unit_at(i);
                opts.push_back(o);
                if((i + 1) == c.get_unit_count())
                    continue;
                schedule_dep dep(
                    c.get_unit_at(i),
                    c.get_unit_at(i + 1),
                    schedule_dep::order_dep);
                cpy->add_dependency(dep);
                o.type = dag_printer_opt::po_color_dep;
                o.color_dep.color = "green";
                o.color_dep.dep = dep;
                o.color_dep.match_all = false;
                opts.push_back(o);
            }
            debug_view_dag(*cpy, opts);
        }
        assert(unit && "no schedulable unit, backtracking not handled yet");

        std::map< schedule_dep::reg_t, std::set< const schedule_unit * > >::iterator it;
        for(it = live_reg_uses.begin(); it != live_reg_uses.end(); ++it)
            it->second.erase(unit);
        for(size_t j = 0; j < dag.get_succs(unit).size(); j++)
        {
            const schedule_dep& dep = dag.get_succs(unit)[j];
            if(!dep.is_phys())
                continue;
            live_reg_uses[dep.reg()].insert(dep.to());
        }
        c.append_unit(unit);
        dag.remove_unit(unit);
    }

    delete cpy;

    STM_STOP(rand_scheduler)
}

/**
 * simple_rp_scheduler
 */
STM_DECLARE(simple_rp_scheduler)

/* represent a live register */
struct srp_live_reg
{
    /* id of the register */
    schedule_dep::reg_t id;
    /* number of remaining use */
    size_t nb_use_left;
};

/* information about a schedule unit */
struct srp_unit_info
{
    /* number of unscheduled dependencies */
    size_t nb_unscheduled_deps;
};

simple_rp_scheduler::simple_rp_scheduler()
{
}

simple_rp_scheduler::~simple_rp_scheduler()
{
}

void simple_rp_scheduler::schedule(schedule_dag& dag, schedule_chain& c) const
{
    debug() << "---> simple_rp_scheduler::schedule\n";
    STM_START(simple_rp_scheduler)
    /* data structures used during the scheduling */
    std::map< const schedule_unit *, srp_unit_info > unit_info;
    std::map< schedule_dep::reg_t, srp_live_reg > live_regs;
    std::vector< const schedule_unit * > schedulable;
    size_t max_rp = 0;
    generic_schedule_chain gsc;

    /* fill info */
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        const schedule_unit *unit = dag.get_units()[u];
        unit_info[unit].nb_unscheduled_deps =
            dag.get_reachable(unit, schedule_dag::rf_follow_preds | schedule_dag::rf_immediate).size();
    }

    schedulable = dag.get_roots();

    /* do the schedule */
    while(schedulable.size() > 0)
    {
        /* find best node to schedule */
        size_t best_idx = 0;
        int best_score = INT_MAX;

        for(size_t i = 0; i < schedulable.size(); i++)
        {
            const schedule_unit *unit = schedulable[i];
            std::set< schedule_dep::reg_t > phys_create = dag.get_reg_phys_create(unit);
            std::set< schedule_dep::reg_t > use = dag.get_reg_use(unit);
            /* first check that the node can be schedule: it must not create a physical register already in use
             * except if it also kills it, tricky ! */
            bool not_schedulable = false;
            for(std::set< schedule_dep::reg_t >::iterator it = phys_create.begin(); it != phys_create.end(); ++it)
            {
                if(live_regs.find(*it) == live_regs.end())
                    continue; /* safe */
                /* unsafe, it create a physical register so it must kill it otherwise it's not a valid schedule:
                 * 1) it must be in the reg use list
                 * 2) it must be the last use of it */
                if(use.find(*it) != use.end())
                {
                    assert(live_regs.find(*it) != live_regs.end() && "Use variable is not alive");
                    if(live_regs[*it].nb_use_left == 1)
                        continue;
                }
                
                not_schedulable = true;
                break;
            }
            if(not_schedulable)
                continue;
            /* if there are dangerous phys, check the instruction also destroy them */
            /* compute score : max(irp, created_reg) - destroyed_reg */
            int score =
                std::max((int)unit->internal_register_pressure(),
                    (int)dag.get_reg_create(unit).size());
            /* loop through each variable used and determine if it is destroyed or not */
            
            for(std::set< schedule_dep::reg_t >::iterator it = use.begin(); it != use.end(); ++it)
            {
                assert(live_regs.find(*it) != live_regs.end() && "Use variable is not alive");
                assert(live_regs[*it].id == *it && "Inconsistent live reg map");
                assert(live_regs[*it].nb_use_left > 0 && "Too many use of register");
                /* if it's the last use... */
                if(live_regs[*it].nb_use_left == 1)
                    score--;
            }

            if(score < best_score)
            {
                best_score = score;
                best_idx = i;
            }
        }
        if(best_score == INT_MAX)
        {
            std::vector< dag_printer_opt > opts;
            schedule_dag *cpy = dag.dup();
            for(size_t i = 0; i < gsc.get_unit_count(); i++)
            {
                dag_printer_opt o;
                o.type = dag_printer_opt::po_color_node;
                o.color_node.color = "magenta";
                o.color_node.unit = gsc.get_unit_at(i);
                opts.push_back(o);
                if((i + 1) == gsc.get_unit_count())
                    continue;
                schedule_dep dep(
                    gsc.get_unit_at(i),
                    gsc.get_unit_at(i + 1),
                    schedule_dep::order_dep);
                cpy->add_dependency(dep);
                o.type = dag_printer_opt::po_color_dep;
                o.color_dep.color = "green";
                o.color_dep.dep = dep;
                o.color_dep.match_all = false;
                opts.push_back(o);
            }
            debug_view_dag(*cpy, opts);
            delete cpy;
        }
        assert(best_score != INT_MAX && "no schedulable unit ?");
        /* Schedule it ! */
        
        /* remove from schedulable list */
        const schedule_unit *unit = schedulable[best_idx];
        unordered_vector_remove(best_idx, schedulable);

        debug() << "  * schedule " << unit->to_string() << "\n";

        gsc.append_unit(unit);

        /* update children */
        std::set< const schedule_unit * > next = dag.get_reachable(unit,
            schedule_dag::rf_follow_succs | schedule_dag::rf_immediate);
        for(std::set< const schedule_unit * >::iterator it = next.begin(); it != next.end(); ++it)
        {
            /* decrement the number of unscheduled deps */
            assert(unit_info.find(*it) != unit_info.end() && "Inconsistent unit info map");
            assert(unit_info[*it].nb_unscheduled_deps > 0 && "Too few unschedule deps");

            unit_info[*it].nb_unscheduled_deps--;
            /* Perhaps unit is schedulable now ? */
            if(unit_info[*it].nb_unscheduled_deps == 0)
            {
                debug() << "    * release " << (*it)->to_string() << "\n";
                schedulable.push_back(*it);
            }
        }

        /* kills registers if needed */
        std::set< schedule_dep::reg_t > use = dag.get_reg_use(unit);
        std::vector< schedule_dep::reg_t > destroyed;
        for(std::set< schedule_dep::reg_t >::iterator it = use.begin(); it != use.end(); ++it)
        {
            live_regs[*it].nb_use_left--;
            /* register is dead ? */
            if(live_regs[*it].nb_use_left == 0)
            {
                debug() << "    * kill r" << *it << "\n";
                /* can't remove during the loop */
                destroyed.push_back(*it);
            }
        }
        /* clear them from the list */
        for(size_t i = 0; i < destroyed.size(); i++)
            live_regs.erase(destroyed[i]);

        /* update RP using IRP */
        max_rp = std::max(max_rp, live_regs.size() + unit->internal_register_pressure());

        /* create registers if needed */
        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            const schedule_dep& dep = dag.get_succs(unit)[i];
            if(!dep.is_data())
                continue;
            if(live_regs.find(dep.reg()) == live_regs.end())
            {
                debug() << "    * create r" << dep.reg() << "\n";
                srp_live_reg slr;
                slr.id = dep.reg();
                slr.nb_use_left = 0;
                live_regs[dep.reg()] = slr;
            }
            /* increase number of uses */
            live_regs[dep.reg()].nb_use_left++;
        }

        /* update RP */
        max_rp = std::max(max_rp, live_regs.size());
    }

    assert(gsc.check_against_dag(dag) && "invalid schedule");
    c.insert_units_at(c.get_unit_count(), gsc.get_units());

    STM_STOP(simple_rp_scheduler)
    debug() << "<--- simple_rp_scheduler::schedule\n";
}

}
