#include "sched-dag.hpp"
#include "sched-dag-viewer.hpp"
#include "tools.hpp"
#include "time-tools.hpp"
#include "adt.hpp"
#include <stdexcept>
#include <queue>
#include <iostream>
#include <cassert>

#ifdef ENABLE_DAG_AUTO_CHECK_CONSISTENCY
#define AUTO_CHECK_CONSISTENCY
#endif

namespace PAMAURY_SCHEDULER_NS
{

/**
 * Interface Implementation
 */
schedule_dag::schedule_dag()
{
}

schedule_dag::~schedule_dag()
{
}

schedule_dag *schedule_dag::deep_dup() const
{
    /* poor's man deep_dup */
    schedule_dag *dag = dup();
    dag->clear();
    
    std::map< const schedule_unit *, const schedule_unit *> map;

    for(size_t u = 0; u < get_units().size(); u++)
    {
        const schedule_unit *unit = get_units()[u];
        map[unit] = unit->deep_dup();
        dag->add_unit(map[unit]);
    }

    std::vector< schedule_dep > to_add;
    for(size_t i = 0; i < get_deps().size(); i++)
    {
        schedule_dep dep = get_deps()[i];
        dep.set_from(map[dep.from()]);
        dep.set_to(map[dep.to()]);
        to_add.push_back(dep);
    }
    dag->add_dependencies(to_add);
    return dag;
}

void schedule_dag::add_dependencies(const std::vector< schedule_dep >& deps)
{
    for(size_t i = 0; i < deps.size(); i++)
        add_dependency(deps[i]);
}

void schedule_dag::remove_dependencies(const std::vector< schedule_dep >& deps)
{
    for(size_t i = 0; i < deps.size(); i++)
        remove_dependency(deps[i]);
}

void schedule_dag::add_units(const std::vector< const schedule_unit * >& units)
{
    for(size_t i = 0; i < units.size(); i++)
        add_unit(units[i]);
}

void schedule_dag::remove_units(const std::vector< const schedule_unit * >& units)
{
    for(size_t i = 0; i < units.size(); i++)
        remove_unit(units[i]);
}

void schedule_dag::modify_dep(const schedule_dep& old, const schedule_dep& cur)
{
    /* trivial implementation; defeats the point of having a specialized function */
    remove_dependency(old);
    add_dependency(cur);
}

std::set< const schedule_unit * > schedule_dag::get_reachable(const schedule_unit *unit,
    unsigned flags) const
{
    std::set< const schedule_unit * > s;
    std::queue< const schedule_unit * > q;

    q.push(unit);

    while(!q.empty())
    {
        const schedule_unit * u = q.front();
        q.pop();
        /* skip unit if already in reachable set */
        if(s.find(u) != s.end())
            continue;
        /* insert it in the list if it's not the intial unit or if
         * the rf_include_unit flag was specified */
        if((flags & rf_include_unit) || u != unit)
            s.insert(u);
        /* stop here if user asked for immediate neighbourhood and
         * the unit is not the intial one */
        if((flags & rf_immediate) && u != unit)
            continue;

        /* propagate to children */
        for(size_t i = 0; i < get_preds(u).size(); i++)
        {
            const schedule_dep& d = get_preds(u)[i];
            if(d.is_data() && (flags & rf_follow_preds_data))
                q.push(d.from());
            else if(d.is_order() && (flags & rf_follow_preds_order))
                q.push(d.from());
        }

        for(size_t i = 0; i < get_succs(u).size(); i++)
        {
            const schedule_dep& d = get_succs(u)[i];
            if(d.is_data() && (flags & rf_follow_succs_data))
                q.push(d.to());
            else if(d.is_order() && (flags & rf_follow_succs_order))
                q.push(d.to());
        }
    }

    return s;
}

std::set< schedule_dep::reg_t > schedule_dag::get_reg_create(
    const schedule_unit *unit, bool pick_virt, bool pick_phys) const
{
    std::set< schedule_dep::reg_t > s;
    for(size_t i = 0; i < get_succs(unit).size(); i++)
        if(get_succs(unit)[i].is_virt() && pick_virt)
            s.insert(get_succs(unit)[i].reg());
        else if(get_succs(unit)[i].is_phys() && pick_phys)
            s.insert(get_succs(unit)[i].reg());
    return s;
}

std::set< schedule_dep::reg_t > schedule_dag::get_reg_phys_create(
    const schedule_unit *unit) const
{
    return get_reg_create(unit, false, true);
}

std::set< schedule_dep::reg_t > schedule_dag::get_reg_virt_create(
    const schedule_unit *unit) const
{
    return get_reg_create(unit, true, false);
}

std::set< schedule_dep::reg_t > schedule_dag::get_reg_use(
    const schedule_unit *unit, bool pick_virt, bool pick_phys) const
{
    std::set< schedule_dep::reg_t > s;
    for(size_t i = 0; i < get_preds(unit).size(); i++)
        if(get_preds(unit)[i].is_phys() && pick_phys)
            s.insert(get_preds(unit)[i].reg());
        else if(get_preds(unit)[i].is_virt() && pick_virt)
            s.insert(get_preds(unit)[i].reg());
    return s;
}

std::set< schedule_dep::reg_t > schedule_dag::get_reg_phys_use(
    const schedule_unit *unit) const
{
    return get_reg_use(unit, false, true);
}

std::set< schedule_dep::reg_t > schedule_dag::get_reg_virt_use(
    const schedule_unit *unit) const
{
    return get_reg_use(unit, true, false);
}

std::set< schedule_dep::reg_t > schedule_dag::get_reg_destroy(
    const schedule_unit *unit) const
{
    std::set< schedule_dep::reg_t > s;
    for(size_t i = 0; i < get_preds(unit).size(); i++)
    {
        const schedule_dep& dep = get_preds(unit)[i];
        if(!dep.is_data())
            continue;
        for(size_t j = 0; j < get_succs(dep.from()).size(); j++)
        {
            const schedule_dep& sec_dep = get_succs(dep.from())[j];
            if(sec_dep.is_data() &&
                    sec_dep.reg() == dep.reg() &&
                    sec_dep.to() != unit)
                goto Lnot_destroy;
        }
        s.insert(dep.reg());

        Lnot_destroy:
        continue;
    }
    return s;
}

MTM_STAT(TM_DECLARE(schedule_dag__get_reg_destroy_exact, "mtm-get_reg_destroy_exact"))

std::set< schedule_dep::reg_t > schedule_dag::get_reg_destroy_exact(
    const schedule_unit *unit) const
{
    MTM_STAT(TM_START(schedule_dag__get_reg_destroy_exact))
    std::set< schedule_dep::reg_t > s;
    /* compute backward reachable set of unit U */
    std::set< const schedule_unit * > lazy_set;
    bool has_lazy_set = false;
    
    /* For each predecessor P of U */
    for(size_t i = 0; i < get_preds(unit).size(); i++)
    {
        const schedule_dep& dep = get_preds(unit)[i];
        /* skip dep of it's not a data one */
        if(!dep.is_data())
            continue;
        /* For each successor S of P */
        for(size_t j = 0; j < get_succs(dep.from()).size(); j++)
        {
            const schedule_dep& sec_dep = get_succs(dep.from())[j];
            /* if the P->S link uses the same register as P->U and P<>U
             * then the register has another use, so we need a complete
             * analysis */
            if(sec_dep.is_data() &&
                    sec_dep.reg() == dep.reg() &&
                    sec_dep.to() != unit)
            {
                if(!has_lazy_set)
                {
                    lazy_set = get_reachable(unit, rf_follow_preds);
                    has_lazy_set = true;
                }
                /* if S is in, then it's ok, otherwise, we can't be sure
                 * that U destroys the register */
                if(lazy_set.find(sec_dep.to()) == lazy_set.end())
                    goto Lnot_destroy;
            }
        }
        s.insert(dep.reg());

        Lnot_destroy:
        continue;
    }
    MTM_STAT(TM_STOP(schedule_dag__get_reg_destroy_exact))
    return s;
}

MTM_STAT(TM_DECLARE(schedule_dag__get_reg_dont_destroy_exact, "mtm-get_reg_dont_destroy_exact"))

std::set< schedule_dep::reg_t > schedule_dag::get_reg_dont_destroy_exact(
    const schedule_unit *unit) const
{
    MTM_STAT(TM_START(schedule_dag__get_reg_dont_destroy_exact))
    std::set< schedule_dep::reg_t > s;
    /* compute reachable set of unit U */
    std::set< const schedule_unit * > lazy_set;
    bool has_lazy_set = false;
    
    /* For each predecessor P of U */
    for(size_t i = 0; i < get_preds(unit).size(); i++)
    {
        const schedule_dep& dep = get_preds(unit)[i];
        /* skip dep of it's not a data one */
        if(!dep.is_data())
            continue;
        /* For each successor S of P */
        for(size_t j = 0; j < get_succs(dep.from()).size(); j++)
        {
            const schedule_dep& sec_dep = get_succs(dep.from())[j];
            /* if the P->S link uses the same register as P->U and P<>U
             * then the register has another use, so we need a complete
             * analysis */
            if(sec_dep.is_data() &&
                    sec_dep.reg() == dep.reg() &&
                    sec_dep.to() != unit)
            {
                if(!has_lazy_set)
                {
                    lazy_set = get_reachable(unit, rf_follow_succs);
                    has_lazy_set = true;
                }
                /* if S is in reachable set of U, then the register is
                 * not destroyed by U because is used later on */
                if(lazy_set.find(sec_dep.to()) != lazy_set.end())
                    goto Lnot_destroy;
            }
        }
        /* if we could not find any other later use, we can't be sure
         * it is not destroyed */
        continue;

        Lnot_destroy:
        s.insert(dep.reg());
        continue;
    }
    MTM_STAT(TM_STOP(schedule_dag__get_reg_dont_destroy_exact))
    return s;
}

MTM_STAT(TM_DECLARE(schedule_dag__fuse_units, "mtm-fuse_units"))

chain_schedule_unit *schedule_dag::fuse_units(const schedule_unit *a,
        const schedule_unit *b, bool simulate_if_approx, bool allow_unsafe_phys_dep_hiding)
{
    MTM_STAT(TM_START(schedule_dag__fuse_units))
    /* compute IRP */
    std::set< schedule_dep::reg_t > vc = get_reg_create(a);
    std::set< schedule_dep::reg_t > vu = get_reg_use(b);
    std::set< schedule_dep::reg_t > vd = get_reg_destroy_exact(b);
    std::set< schedule_dep::reg_t > vdd = get_reg_dont_destroy_exact(b);

    std::set< schedule_dep::reg_t > vu_min_vc_min_vdd = set_minus(set_minus(vu, vc), vdd);
    std::set< schedule_dep::reg_t > vu_plus__p_vc_min_vdd_p = set_union(vc, set_minus(vu, vdd));
    std::set< schedule_dep::reg_t > vc_min_vd = set_minus(vc, vd);

    /* Check for phys deps hiding */
    if(!allow_unsafe_phys_dep_hiding)
    {
        /* the A or B unit must not use any physical register */
        if(!get_reg_phys_use(b).empty() ||
                !get_reg_phys_use(a).empty() ||
                !get_reg_phys_create(a).empty() ||
                !get_reg_phys_create(b).empty())
            return 0;
    }

    /**
     * The IRP of the new units has to take into account three points
     * 1) The first point is the pressure when a is being executed,
     *    then the IRP is the sume of IRP(A) plus some pressure induced
     *    by the registers used by B. The problem is that if we add to
     *    IRP(A) the number of registers used by b which are not created
     *    A then we are doing an overapproximation. Indeed, if some
     *    register, say R1 is created by a unit C and then used by B
     *    and by another unit D and if for some reason D has to be 
     *    executed after B then R1 will be counted twice:
     *    - once in IRP(fuse(A,B))
     *    - once because of the data link between C and D
     *    In this case we can remove R1 from the IRP because we _KNOW_
     *    that R1 is not destroyed by B and used _LATER_.
     *    So we could say that the variable used by B can be partionned
     *    in four sets:
     *    - set of variables which were created by A => see 2)
     *    - set of variables which are destroyed by B for sure =>
     *      these must be counted in the IRP because they won't count
     *      elsewhere
     *    - set of variable which are not destroyed by B for sure =>
     *      these must mot be counted in the IRP because we know they
     *      are used later (ie B is sure to not destroy them) and will
     *      already be taken into account by data deps
     *    - set of variable which can or cannot be destroyed by B =>
     *      we must count them if we want an _OVERAPPROXIMATION_
     * 2) The second point is between the execution of A and B.
     *    The internal vairables alive is the set of variable created
     *    by A plus the set of variable used by B (make the union)
     * 3) The third point is during the execution of B: the must add
     *    IRP(B) with the set of variable created by A and which have
     *    not been destroyed by B
     */

    /* Optimality check */
    {
        /**
         * We require that each variable use by B either
         * - is created by A
         * - is destroyed for sure by B
         * - is not destroyed for sure by B
         * If there is variable for which we are not sure if B destroy it
         * or not, then it's an overapproximation
         */
        std::set< schedule_dep::reg_t > vu_min_vc_min_vdd_min_vd = set_minus(vu_min_vc_min_vdd, vd);
        if(vu_min_vc_min_vdd_min_vd.size() > 0)
        {
            /*
            debug() << "Overapproximation in fuse units:\n";
            debug() << "  Unit: " << a->to_string() << "\n";
            debug() << "  Unit: " << b->to_string() << "\n";
            debug() << "  VC=" << vc << "\n";
            debug() << "  VU=" << vu << "\n";
            debug() << "  VD=" << vd << "\n";
            debug() << "  VDD=" << vdd << "\n";
            */
            if(simulate_if_approx)
            {
                MTM_STAT(TM_STOP(schedule_dag__fuse_units))
                return 0;
            }
        }
    }

    /* create new unit */
    chain_schedule_unit *c = new chain_schedule_unit;
    c->get_chain().push_back(a);
    c->get_chain().push_back(b);

    /* create new dependencies */
    std::vector< schedule_dep > to_add;
    for(size_t i = 0; i < get_preds(a).size(); i++)
    {
        schedule_dep dep = get_preds(a)[i];
        dep.set_to(c);
        to_add.push_back(dep);
    }
    for(size_t i = 0; i < get_succs(a).size(); i++)
    {
        schedule_dep dep = get_succs(a)[i];
        /* avoid self loop */
        if(dep.to() == b)
            continue;
        dep.set_from(c);
        to_add.push_back(dep);
    }
    for(size_t i = 0; i < get_preds(b).size(); i++)
    {
        schedule_dep dep = get_preds(b)[i];
        /* avoid self loop */
        if(dep.from() == a)
            continue;
        dep.set_to(c);
        to_add.push_back(dep);
    }
    for(size_t i = 0; i < get_succs(b).size(); i++)
    {
        schedule_dep dep = get_succs(b)[i];
        dep.set_from(c);
        to_add.push_back(dep);
    }

    c->set_internal_register_pressure(
        std::max(a->internal_register_pressure() + vu_min_vc_min_vdd.size(),
        std::max(vu_plus__p_vc_min_vdd_p.size(),
                b->internal_register_pressure() + vc_min_vd.size())));

    #if 0
    {
        std::vector< pasched::dag_printer_opt > opts;
        dag_printer_opt o;
        o.type = dag_printer_opt::po_color_node;
        o.color_node.unit = a;
        o.color_node.color = "red";
        opts.push_back(o);
        o.color_node.unit = b;
        opts.push_back(o);
        debug_view_dag(*this, opts);
    }
    #endif


    /* change the graph */
    add_unit(c);
    remove_unit(a);
    remove_unit(b);
    add_dependencies(to_add);
    
    /* remove redundant data deps */
    remove_redundant_data_dep_preds(c);

    MTM_STAT(TM_STOP(schedule_dag__fuse_units))

    return c;
}

schedule_dag *schedule_dag::dup_subgraph(const std::set< const schedule_unit * >& sub) const
{
    /* Do it the dirty and inefficient way: duplicate this DAG and remove the useless stuff */
    schedule_dag *cpy = dup();

    for(size_t u = 0; u < get_units().size(); u++)
        if(sub.find(get_units()[u]) == sub.end())
            cpy->remove_unit(get_units()[u]);
    return cpy;
}


void schedule_dag::collapse_subgraph(const std::set< const schedule_unit * >& sub,
        const schedule_unit *new_unit)
{
    std::vector< schedule_dep > to_add;

    for(size_t u = 0; u < get_units().size(); u++)
    {
        const schedule_unit *unit = get_units()[u];
        /* ignore unit if it's not in the subgraph */
        if(sub.find(unit) == sub.end())
            continue;

        for(size_t i = 0; i < get_preds(unit).size(); i++)
        {
            schedule_dep dep = get_preds(unit)[i];
            /* dependency must come from outside of the subgraph */
            if(sub.find(dep.from()) != sub.end())
                continue;
            dep.set_to(new_unit);
            to_add.push_back(dep);
        }
        for(size_t i = 0; i < get_succs(unit).size(); i++)
        {
            schedule_dep dep = get_succs(unit)[i];
            /* dependency must go outside of the subgraph */
            if(sub.find(dep.to()) != sub.end())
                continue;
            dep.set_from(new_unit);
            to_add.push_back(dep);
        }
    }

    /* remove all units from the subgraph */
    remove_units(set_to_vector(sub));
    /* add the new unit */
    add_unit(new_unit);
    /* add new dependencies */
    add_dependencies(to_add);
}

void schedule_dag::replace_unit(const schedule_unit *old, const schedule_unit *new_unit)
{
    /* quick and dirty solution */
    std::set< const schedule_unit * > sub;
    sub.insert(old);

    collapse_subgraph(sub, new_unit);
}

schedule_dep::reg_t schedule_dag::generate_unique_reg_id() const
{
    return schedule_dep::generate_unique_reg_id();
}

void schedule_dag::remove_redundant_data_dep_preds(const schedule_unit *unit)
{
    std::vector< schedule_dep > to_remove;
    
    for(size_t i = 0; i < get_preds(unit).size(); i++)
    {
        const schedule_dep& dep = get_preds(unit)[i];
        if(!dep.is_data())
            continue;
        for(size_t j = i + 1; j < get_preds(unit).size(); j++)
        {
            const schedule_dep& dep2 = get_preds(unit)[j];
            if(!dep2.is_data())
                continue;
            if(dep.reg() != dep2.reg())
                continue;
            if(dep.from() != dep2.from())
            {
                std::vector< dag_printer_opt > opts;
                dag_printer_opt o;
                o.type = dag_printer_opt::po_color_dep;
                o.color_dep.match_all = false;
                o.color_dep.dep = dep;
                o.color_dep.color = "green";
                opts.push_back(o);
                o.color_dep.dep = dep2;
                opts.push_back(o);
                debug_view_dag(*this, opts);
                throw std::runtime_error("schedule_dep::remove_redundant_data_dep_preds detected several creators for the same register");
            }
            to_remove.push_back(dep2);
            /* stop now because otherwise, we will remove some twice or more ! */
            break;
        }
    }

    remove_dependencies(to_remove);
}

void schedule_dag::remove_redundant_data_dep_succs(const schedule_unit *unit)
{
    std::vector< schedule_dep > to_remove;
    
    for(size_t i = 0; i < get_succs(unit).size(); i++)
    {
        const schedule_dep& dep = get_succs(unit)[i];
        if(!dep.is_data())
            continue;
        for(size_t j = i + 1; j < get_succs(unit).size(); j++)
        {
            const schedule_dep& dep2 = get_succs(unit)[j];
            if(!dep2.is_data())
                continue;
            if(dep.reg() == dep2.reg() && dep.to() == dep2.to())
            {
                to_remove.push_back(dep2);
                /* stop now because otherwise, we will remove some twice or more ! */
                break;
            }
        }
    }

    remove_dependencies(to_remove);
}

void schedule_dag::remove_redundant_data_deps()
{
    for(size_t u = 0; u < get_units().size(); u++)
        remove_redundant_data_dep_succs(get_units()[u]);
}

namespace
{
    void compute_path_map(
        const schedule_dag& dag,
        std::vector< std::vector< bool > >& path,
        std::map< const schedule_unit *, size_t >& name_map,
        std::map< const schedule_unit *, bitmap >& reach,
        const schedule_unit *unit)
    {
        if(reach.find(unit) != reach.end())
            return;
        bitmap set(dag.get_units().size());
        set.set_bit(name_map[unit]);

        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            const schedule_unit *next = dag.get_succs(unit)[i].to();
            compute_path_map(dag, path, name_map, reach, next);
            
            bitmap& rset = reach[next];
            set |= rset;
        }

        for(bitmap::const_bit_set_iterator it = set.bit_set_begin(); it != set.bit_set_end(); ++it)
            path[name_map[unit]][*it] = true;

        reach[unit] = set;
    }
}

MTM_STAT(TM_DECLARE(schedule_dag__build_path_map, "mtm-build_path_map"))

void schedule_dag::build_path_map(std::vector< std::vector< bool > >& path,
        std::map< const schedule_unit *, size_t >& name_map) const
{
    MTM_STAT(TM_START(schedule_dag__build_path_map))
    /* build name map and resize path */
    path.resize(get_units().size());
    name_map.clear();
    
    for(size_t u = 0; u < get_units().size(); u++)
    {
        name_map[get_units()[u]] = u;
        path[u].resize(get_units().size());
    }

    /* compute path map */
    std::map< const schedule_unit *, bitmap > reach;

    for(size_t i = 0; i < get_roots().size(); i++)
        compute_path_map(*this, path, name_map, reach, get_roots()[i]);

    MTM_STAT(TM_STOP(schedule_dag__build_path_map))
    debug() << "schedule_dag__build_path_map: " <<
        schedule_dag__build_path_map.get_timer().get_value() / (float)schedule_dag__build_path_map.get_timer().get_hz() << "\n";
    debug() << "  #nodes: " << get_units().size() << "\n";
    debug() << "  #edges: " << get_deps().size() << "\n";
}

/**
 * Generic Implementation
 */
generic_schedule_dag::generic_schedule_dag()
{
    set_modified(false);
}


generic_schedule_dag::~generic_schedule_dag()
{
}

generic_schedule_dag *generic_schedule_dag::dup() const
{
    return new generic_schedule_dag(*this);
}

bool generic_schedule_dag::modified() const
{
    return m_modified;
}

void generic_schedule_dag::set_modified(bool mod)
{
    m_modified = mod;
}

void generic_schedule_dag::add_unit(const schedule_unit *u)
{
    m_units.push_back(u);
    m_roots.push_back(u);
    m_leaves.push_back(u);
    m_unit_map.insert(std::make_pair(u, su_data()));
    
    set_modified(true);

    #ifdef AUTO_CHECK_CONSISTENCY
    std::string s;
    if(!is_consistent(&s))
        throw std::runtime_error("DAG is not consistent after add_unit (" + s + ")");
    #endif
}

struct compare_from_to_against
{
    compare_from_to_against(const schedule_unit *u) : u(u) {}
    bool operator()(const schedule_dep& d) const { return d.from() == u || d.to() == u; }
    const schedule_unit *u;
};

void generic_schedule_dag::remove_unit(const schedule_unit *u)
{
    compare_from_to_against cfta(u);
    unordered_find_and_remove(cfta, m_deps);
    for(size_t i = 0; i < m_units.size(); i++)
    {
        unordered_find_and_remove(cfta, m_unit_map[m_units[i]].preds);
        unordered_find_and_remove(cfta, m_unit_map[m_units[i]].succs);
    }
    
    unordered_find_and_remove(u, m_units);
    unordered_find_and_remove(u, m_roots);
    unordered_find_and_remove(u, m_leaves);

    m_unit_map.erase(m_unit_map.find(u));

    /* recompute roots and leaves */
    m_roots.clear();
    m_leaves.clear();

    for(size_t i = 0; i < m_units.size(); i++)
    {
        if(m_unit_map[m_units[i]].preds.size() == 0)
            m_roots.push_back(m_units[i]);
        if(m_unit_map[m_units[i]].succs.size() == 0)
            m_leaves.push_back(m_units[i]);
    }

    set_modified(true);

    #ifdef AUTO_CHECK_CONSISTENCY
    std::string s;
    if(!is_consistent(&s))
        throw std::runtime_error("DAG is not consistent after remove_unit (" + s + ")");
    #endif
}

void generic_schedule_dag::add_dependency(schedule_dep d)
{
    m_unit_map[d.from()].succs.push_back(d);
    m_unit_map[d.to()].preds.push_back(d);
    m_deps.push_back(d);

    unordered_find_and_remove(d.from(), m_leaves);
    unordered_find_and_remove(d.to(), m_roots);

    set_modified(true);

    #ifdef AUTO_CHECK_CONSISTENCY
    std::string s;
    if(!is_consistent(&s))
        throw std::runtime_error("DAG is not consistent after add_dependency (" + s + ")");
    #endif
}

void generic_schedule_dag::remove_dependency(schedule_dep d)
{
    /*
     * Warning !
     * If a dependency exists twice, this should remove only one instance !!
     */
    unordered_find_and_remove(d, m_deps, true);
    unordered_find_and_remove(d, m_unit_map[d.from()].succs, true);
    unordered_find_and_remove(d, m_unit_map[d.to()].preds, true);

    if(m_unit_map[d.from()].succs.size() == 0)
        m_leaves.push_back(d.from());
    if(m_unit_map[d.to()].preds.size() == 0)
        m_roots.push_back(d.to());

    set_modified(true);

    #ifdef AUTO_CHECK_CONSISTENCY
    std::string s;
    if(!is_consistent(&s))
        throw std::runtime_error("DAG is not consistent after remove_dependency (" + s + ")");
    #endif
}

void generic_schedule_dag::modify_dep(const schedule_dep& _old, const schedule_dep& cur)
{
    /* make a copy of the old one ! */
    schedule_dep old = _old;
    assert(old.from() == cur.from() && old.to() == cur.to() && "You can't change from/to properties with modify_dep()");

    unordered_find_and_modify(old, cur, m_deps, true);
    unordered_find_and_modify(old, cur, m_unit_map[old.from()].succs, true);
    unordered_find_and_modify(old, cur, m_unit_map[old.to()].preds, true);
    
    #ifdef AUTO_CHECK_CONSISTENCY
    std::string s;
    if(!is_consistent(&s))
        throw std::runtime_error("DAG is not consistent after remove_dependency (" + s + ")");
    #endif
}

namespace
{
    bool find_loop(const schedule_dag& dag, const schedule_unit *unit,
        std::map< const schedule_unit *, int >& status)
    {
        /* 0: not seen
         * 1: processing
         * 2: process */
        if(status[unit] == 1)
            return true;
        if(status[unit] == 2)
            return false;
        status[unit] = 1;

        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
            if(find_loop(dag, dag.get_succs(unit)[i].to(), status))
                return true;
        status[unit] = 2;
        return false;
    }
}

bool generic_schedule_dag::is_consistent(std::string *out_msg) const
{
    #if 1
    #define NOT_CONSISTENT(msg) { if(out_msg) *out_msg = msg; return false; }
    #else
    #define NOT_CONSISTENT(msg) throw std::runtime_error("DAG is not consistent (" msg ")")
    #endif
    // every node in root and leaves must be in the whole list
    // and must be consistent
    for(size_t i = 0; i < m_roots.size(); i++)
    {
        if(!container_contains(m_units, m_roots[i]))
            NOT_CONSISTENT("unit in roots list is not in master list");
        if(m_unit_map[m_roots[i]].preds.size() != 0)
            NOT_CONSISTENT("unit in roots list has predecessors");
    }
    for(size_t i = 0; i < m_leaves.size(); i++)
    {
        if(!container_contains(m_units, m_leaves[i]))
            NOT_CONSISTENT("unit in leaves list is not in master list");
        if(m_unit_map[m_leaves[i]].succs.size() != 0)
            NOT_CONSISTENT("unit in leaves list has successors");
    }
    // check unit dependencies
    for(size_t i = 0; i < m_units.size(); i++)
    {
        const schedule_unit *unit = m_units[i];
        if(m_unit_map.find(unit) == m_unit_map.end())
            NOT_CONSISTENT("unit in master list has no pred/succ info attached");
        su_data& d = m_unit_map[unit];
        for(size_t j = 0; j < d.preds.size(); j++)
        {
            // dependency must go to the unit !
            if(d.preds[j].to() != unit)
                NOT_CONSISTENT("unit has invalid predeccessor dep (it is not the target !)");
            // dependency must come from somewhere
            if(!container_contains(m_units, d.preds[j].from()))
                NOT_CONSISTENT("unit has invalid predeccessor dep (source does not exist !)");
            // dependency must be the global list
            if(!container_contains(m_deps, d.preds[j]))
                NOT_CONSISTENT("unit has invalid predeccessor dep (dep is not in master list)");
        }
        for(size_t j = 0; j < d.succs.size(); j++)
        {
            // dependency must come from the unit !
            if(d.succs[j].from() != unit)
                NOT_CONSISTENT("unit has invalid successor dep (it is not the source !)");
            // dependency must come from somewhere
            if(!container_contains(m_units, d.succs[j].to()))
                NOT_CONSISTENT("unit has invalid predeccessor dep (target does not exist !)");
            // dependency must be the global list
            if(!container_contains(m_deps, d.succs[j]))
                NOT_CONSISTENT("unit has invalid predecessor dep (dep is not in master list)");
        }

        // check root and leaves
        if(d.preds.size() == 0 && !container_contains(m_roots, unit))
            NOT_CONSISTENT("leaf unit in master list is not in leaves list");
        if(d.succs.size() == 0 && !container_contains(m_leaves, unit))
            NOT_CONSISTENT("root unit in master list is not in roots list");
    }
    // check dep
    for(size_t i = 0; i < m_deps.size(); i++)
    {
        const schedule_dep& dep = m_deps[i];

        if(!container_contains(m_unit_map[dep.from()].succs, dep))
            NOT_CONSISTENT("dep in master list is not attached to source");
        if(!container_contains(m_unit_map[dep.to()].preds, dep))
            NOT_CONSISTENT("dep in master list is not attached to target");
    }
    // check loops
    std::map< const schedule_unit *, int > status;

    for(size_t i = 0; i < get_units().size(); i++)
        status[get_units()[i]] = 0;

    for(size_t i = 0; i < get_roots().size(); i++)
        if(find_loop(*this, get_roots()[i], status))
            NOT_CONSISTENT("loop in DAG");

    return true;
}

void generic_schedule_dag::clear()
{
    m_units.clear();
    m_leaves.clear();
    m_roots.clear();
    m_unit_map.clear();
    m_deps.clear();
    set_modified(true);
}

}
