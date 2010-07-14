#include <sched-transform.hpp>
#include <tools.hpp>
#include <sstream>
#include <stdexcept>
#include <cassert>
#include <algorithm>
#include <iostream>

namespace PAMAURY_SCHEDULER_NS
{

/**
 * transformation_status
 */
transformation_status::transformation_status()
{
}

transformation_status::~transformation_status()
{
}

/**
 * basic_status
 */
basic_status::basic_status()
{
}

basic_status::~basic_status()
{
}

void basic_status::begin_transformation()
{
    m_mod = m_junction = m_deadlock = false;
}

void basic_status::end_transformation()
{
}

void basic_status::set_modified_graph(bool m)
{
    m_mod = m;
}

bool basic_status::has_modified_graph() const
{
    return m_mod;
}

void basic_status::set_deadlock(bool d)
{
    if(d)
        set_junction(false);
    m_deadlock = d;
}

bool basic_status::is_deadlock() const
{
    return m_deadlock;
}

void basic_status::set_junction(bool j)
{
    if(j)
        set_deadlock(false);
    m_junction = j;
}

bool basic_status::is_junction() const
{
    return m_junction;
}

/**
 * transformation
 */
transformation::transformation()
{
}

transformation::~transformation()
{
}

/**
 * glued_transformation_scheduler
 */

glued_transformation_scheduler::glued_transformation_scheduler(const transformation *transform, const scheduler *sched,
    transformation_status& status)
    :m_transform(transform), m_scheduler(sched), m_status(status)
{
}

glued_transformation_scheduler::~glued_transformation_scheduler()
{
}

void glued_transformation_scheduler::schedule(schedule_dag& d, schedule_chain& c) const
{
     m_transform->transform(d, *m_scheduler, c, m_status);
}

/**
 * packed_status
 */
packed_status::packed_status(transformation_status& status)
    :m_level(0), m_status(status)
{
}

packed_status::~packed_status()
{
    if(m_level != 0)
        throw std::runtime_error("packed_status::~packed_status detected an invalid stacking level");
}

void packed_status::begin_transformation()
{
    if(m_level == 0)
        m_status.begin_transformation();
    m_level++;
}

void packed_status::end_transformation()
{
    m_level--;
    if(m_level == 0)
        m_status.end_transformation();
}

void packed_status::set_modified_graph(bool m)
{
    if(m)
        m_status.set_modified_graph(true);
}

bool packed_status::has_modified_graph() const
{
    return m_status.has_modified_graph();
}

void packed_status::set_deadlock(bool d)
{
    /* don't keep track of deadlock */
}

bool packed_status::is_deadlock() const
{
    return false;
}

void packed_status::set_junction(bool j)
{
    if(j)
        m_status.set_junction(true);
}

bool packed_status::is_junction() const
{
    return m_status.is_junction();
}

/**
 * packed_transformation
 */
packed_transformation::packed_transformation(const transformation *first, const transformation *second)
    :m_first(first), m_second(second)
{
}

packed_transformation::~packed_transformation()
{
}

void packed_transformation::transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    packed_status ps(status);
    glued_transformation_scheduler internal_sched(m_second, &s, ps);
    m_first->transform(d, internal_sched, c, ps);
}

/**
 * transformation_pipeline
 */

transformation_pipeline::transformation_pipeline()
{
}

transformation_pipeline::~transformation_pipeline()
{
    for(size_t i = 0; i < m_packers.size(); i++)
        delete m_packers[i];
}

void transformation_pipeline::add_stage(const transformation *transform)
{
    m_pipeline.push_back(transform);
    if(m_packers.size() > 0)
        m_packers.push_back(new packed_transformation(m_packers.back(), transform));
    else if(m_pipeline.size() == 2)
        m_packers.push_back(new packed_transformation(m_pipeline[0], m_pipeline[1]));
}

void transformation_pipeline::transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    if(m_pipeline.size() == 0)
        throw std::runtime_error("transformation_pipeline::transform called with empty pipeline");
    if(m_packers.size() > 0)
        return m_packers.back()->transform(d, s, c, status);
    else
        return m_pipeline[0]->transform(d, s, c, status);
}

/**
 * transformation_loop
 */
transformation_loop::transformation_loop(const transformation *x)
    :m_transform(x)
{
}

transformation_loop::~transformation_loop()
{
}

void transformation_loop::transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    
}

/**
 * unique_reg_ids
 */

unique_reg_ids::unique_reg_ids()
{
}

unique_reg_ids::~unique_reg_ids()
{
}

void unique_reg_ids::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    std::vector< schedule_dep > to_remove;
    std::vector< schedule_dep > to_add;

    status.begin_transformation();
    
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        const schedule_unit *unit = dag.get_units()[u];
        std::map< schedule_dep::reg_t, schedule_dep::reg_t > reg_map;
        
        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            schedule_dep dep = dag.get_succs(unit)[i];
            to_remove.push_back(dep);

            if(dep.kind() == schedule_dep::data_dep)
            {
                if(reg_map.find(dep.reg()) == reg_map.end())
                    reg_map[dep.reg()] = dag.generate_unique_reg_id();
            
                dep.set_reg(reg_map[dep.reg()]);
            }
            to_add.push_back(dep);
        }
    }

    dag.remove_dependencies(to_remove);
    dag.add_dependencies(to_add);

    status.set_modified_graph(true);
    status.set_deadlock(false);
    status.set_junction(false);

    s.schedule(dag, c);

    status.end_transformation();
}

/**
 * unique_reg_ids
 */

strip_useless_order_deps::strip_useless_order_deps()
{
}

strip_useless_order_deps::~strip_useless_order_deps()
{
}

void strip_useless_order_deps::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    /* Shortcuts */
    const std::vector< const schedule_unit * >& units = dag.get_units();
    size_t n = units.size();
    /* Map a unit pointer to a number */
    std::map< const schedule_unit *, size_t > name_map;
    /* path[u][v] is true if there is a path from u to v */
    std::vector< std::vector< bool > > path;
    std::vector< schedule_dep > to_remove;

    status.begin_transformation();

    /* As a first step, remove duplicate order, that is when
     * there are several order deps between same pair of units */
    for(size_t u = 0; u < n; u++)
    {
        const schedule_unit *unit = units[u];
        const std::vector< schedule_dep >& succs = dag.get_succs(unit);
        for(size_t i = 0; i < succs.size(); i++)
            for(size_t j = i + 1; j < succs.size(); j++)
                if(succs[i].kind() == schedule_dep::order_dep &&
                        succs[j].kind() == schedule_dep::order_dep &&
                        succs[i].to() == succs[j].to())
                {
                    to_remove.push_back(succs[j]);
                    break;
                }
    }

    dag.remove_dependencies(to_remove);
    to_remove.clear();

    path.resize(n);
    for(size_t u = 0; u < n; u++)
    {
        name_map[units[u]] = u;
        path[u].resize(n);
    }

    /* Fill path */
    for(size_t u = 0; u < n; u++)
    {
        std::set< const schedule_unit * > reach = dag.get_reachable(units[u],
            schedule_dag::rf_follow_succs | schedule_dag::rf_include_unit);
        std::set< const schedule_unit * >::iterator it = reach.begin();
        
        for(; it != reach.end(); ++it)
            path[u][name_map[*it]] = true;
    }

    

    /* To remove a dependency, we go through each node U
     * If for such a node, there are two dependencies A->U and B->U
     * such that there is a path from A to B (A--->B), and A->U is an
     * order dep, then A->U is useless */
    for(size_t u = 0; u < n; u++)
    {
        const schedule_unit *unit = units[u];
        const std::vector< schedule_dep >& preds = dag.get_preds(unit);

        /* Loop through each pair of dep (A->U,B->U) */
        for(size_t i = 0; i < preds.size(); i++)
        {
            size_t i_from = name_map[preds[i].from()];
            /* Mind the order !
             * We don't want to treat each pair twice because we would
             * end up removing each edge twice. Furthermore we should
             * be careful in such a situation:
             * A -> U where are two order dep between A and U
             * then we want to remove only one of them */
            for(size_t j = i + 1; j < preds.size(); j++)
            {
                size_t j_from = name_map[preds[j].from()];

                assert(path[i_from][u] && path[j_from][u]);

                /* Try both order */
                if(path[i_from][j_from] && preds[i].kind() == schedule_dep::order_dep)
                    to_remove.push_back(preds[i]);
                else if(path[j_from][i_from] && preds[j].kind() == schedule_dep::order_dep)
                    to_remove.push_back(preds[j]);
            }
        }
    }

    for(size_t i = 0; i < to_remove.size(); i++)
        dag.remove_dependency(to_remove[i]);

    status.set_modified_graph(to_remove.size() > 0);
    status.set_deadlock(false);
    status.set_junction(false);

    s.schedule(dag, c);

    status.end_transformation();
}

/**
 * smart_fuse_two_units
 */

smart_fuse_two_units::smart_fuse_two_units(bool allow_non_optimal_irp_calculation)
    :m_allow_non_optimal_irp_calculation(allow_non_optimal_irp_calculation)
{
}

smart_fuse_two_units::~smart_fuse_two_units()
{
}

void smart_fuse_two_units::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    /* Do two passes: first don't allow approx and then do so */
    bool allow_approx = false;
    std::vector< chain_schedule_unit * > fused;

    status.begin_transformation();
    
    while(true)
    {        
        for(size_t u = 0; u < dag.get_units().size(); u++)
        {
            const schedule_unit *unit = dag.get_units()[u];

            std::set< schedule_dep::reg_t > vc = dag.get_reg_create(unit);
            std::set< schedule_dep::reg_t > vu = dag.get_reg_use(unit);
            std::set< schedule_dep::reg_t > vd = dag.get_reg_destroy(unit);
            std::set< const schedule_unit * > ipreds = dag.get_reachable(unit,
                schedule_dag::rf_follow_preds | schedule_dag::rf_immediate);
            std::set< const schedule_unit * > isuccs = dag.get_reachable(unit,
                schedule_dag::rf_follow_succs | schedule_dag::rf_immediate);

            /*
            std::cout << "Unit: " << unit << "\n";
            std::cout << "  VC=" << vc << "\n";
            std::cout << "  VU=" << vu << "\n";
            std::cout << "  VD=" << vd << "\n";
            */
            
            /* Case 1
             * - unit has one predecessor only
             * - unit destroys more variable than it creates ones
             * - IRP of unit is lower than the number of destroyed variables
             * Then
             * - fuse unit to predecessor */
            if(ipreds.size() == 1 && vd.size() >= vc.size() &&
                    unit->internal_register_pressure() <= vd.size())
            {
                chain_schedule_unit *c = dag.fuse_units(dag.get_preds(unit)[0].from(), unit, !allow_approx);
                if(c != 0)
                {
                    fused.push_back(c);
                    goto Lgraph_changed;
                }
            }
            /* Case 2
             * - unit has one successor only
             * - unit creates more variable than it uses ones
             * - IRP of unit is lower than the number of created variables
             * Then
             * - fuse unit to successor */
            else if(isuccs.size() == 1 && vc.size() >= vu.size() &&
                    unit->internal_register_pressure() <= vc.size())
            {
                chain_schedule_unit *c = dag.fuse_units(unit, dag.get_succs(unit)[0].to(), !allow_approx);
                if(c != 0)
                {
                    fused.push_back(c);
                    goto Lgraph_changed;
                }
            }
        }
        /* no change, stop ? */
        if(!allow_approx && m_allow_non_optimal_irp_calculation)
        {
            allow_approx = true;
            continue;
        }
        break;
        Lgraph_changed:
        continue;
    }

    status.set_modified_graph(fused.size() > 0);
    status.set_deadlock(false);
    status.set_junction(false);

    /* schedule DAG */
    s.schedule(dag, c);

    /* unfuse units */
    std::reverse(fused.begin(), fused.end());

    for(size_t i = 0; i < fused.size(); i++)
    {
        size_t j;
        for(j = 0; j < c.get_unit_count(); j++)
        {
            if(c.get_unit_at(j) == fused[i])
                break;
        }
        if(j == c.get_unit_count())
            throw std::runtime_error("smart_fuse_two_units::transform detected inconsistent schedule chain");

        c.remove_unit_at(j);
        for(size_t k = 0; k < fused[i]->get_chain().size(); k++)
            c.insert_unit_at(j + k, fused[i]->get_chain()[k]);
        delete fused[i];
    }

    status.end_transformation();
}

/**
 * simplify_order_cuts
 */

simplify_order_cuts::simplify_order_cuts()
{
}

simplify_order_cuts::~simplify_order_cuts()
{
}

void simplify_order_cuts::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    do_transform(dag, s, c, status, 0);
}

void simplify_order_cuts::do_transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status, int level) const
{
    if(level == 0)
        status.begin_transformation();
    
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        /* pick the first unit */
        const schedule_unit *unit = dag.get_units()[u];
        /* Compute the largest component C which
         * contains U and which is stable by these operations:
         * 1) If A is in C and A->B is a data dep, B is in C
         * 2) If A is in C and B->A is a data dep, B is in C
         * 2) If A is in C and B->A is an order dep, B is in C
         */
        std::set< const schedule_unit * > reach = dag.get_reachable(unit,
            schedule_dag::rf_include_unit | schedule_dag::rf_follow_preds | schedule_dag::rf_follow_succs_data);

        /* handle trivial case where the whole graph is reachable */
        if(reach.size() == dag.get_units().size())
            continue;

        /* extract this subgraph for further analysis */
        schedule_dag *top = dag.dup_subgraph(reach);
        dag.remove_units(set_to_vector(reach));

        if(level == 0)
        {
            status.set_modified_graph(true);
            status.set_junction(true); /* don't set deadlock then ! */
        }
        /* recursively transform top */
        do_transform(*top, s, c, status, level + 1);
        /* and then bottom */
        do_transform(dag, s, c, status, level + 1);

        status.end_transformation();
        return;
    }

    if(level == 0)
    {
        status.set_modified_graph(false);
        status.set_deadlock(false);
        status.set_junction(false);
    }
    /* otherwise, schedule the whole graph */
    s.schedule(dag, c);

    if(level == 0)
        status.end_transformation();
}

/**
 * split_def_use_dom_use_deps
 */
split_def_use_dom_use_deps::split_def_use_dom_use_deps(bool generate_new_reg_ids)
    :m_generate_new_reg_ids(generate_new_reg_ids)
{
}

split_def_use_dom_use_deps::~split_def_use_dom_use_deps()
{
}

void split_def_use_dom_use_deps::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    bool graph_changed = false;
    /* Shortcuts */
    const std::vector< const schedule_unit * >& units = dag.get_units();
    size_t n = units.size();
    /* Map a unit pointer to a number */
    std::map< const schedule_unit *, size_t > name_map;
    /* path[u][v] is true if there is a path from u to v */
    std::vector< std::vector< bool > > path;

    status.begin_transformation();

    /* First compute path map, it will not changed during the algorithm
     * even though some edges are changed */
    path.resize(n);
    for(size_t u = 0; u < n; u++)
    {
        name_map[units[u]] = u;
        path[u].resize(n);
    }

    /* Fill path */
    for(size_t u = 0; u < n; u++)
    {
        std::set< const schedule_unit * > reach = dag.get_reachable(units[u],
            schedule_dag::rf_follow_succs | schedule_dag::rf_include_unit);
        std::set< const schedule_unit * >::iterator it = reach.begin();
        for(; it != reach.end(); ++it)
            path[u][name_map[*it]] = true;
        /*
        std::cout << "Reachable from " << units[u]->to_string() << "\n";
        std::cout << "  " << reach << "\n";
        */
    }
    
    /* Each iteration might modify the graph */
    while(true)
    {
        /* For each unit U */
        for(size_t u = 0; u < dag.get_units().size(); u++)
        {
            const schedule_unit *unit = dag.get_units()[u];
            const std::vector< schedule_dep >& succs = dag.get_succs(unit);
            /* Skip useless units for speed reason */
            if(succs.size() <= 1)
                continue;
            /* Compute the set of registers on successors dep */
            std::map< schedule_dep::reg_t, std::vector< schedule_dep > > reg_succs;

            for(size_t i = 0; i < succs.size(); i++)
                if(succs[i].kind() == schedule_dep::data_dep)
                    reg_succs[succs[i].reg()].push_back(succs[i]);

            if(0)
            {
                std::cout << "Unit: " << unit->to_string() << "\n";
                std::map< schedule_dep::reg_t, std::vector< schedule_dep > >::iterator reg_succs_it = reg_succs.begin();
                for(; reg_succs_it != reg_succs.end(); ++reg_succs_it)
                {
                    std::cout << "  Register " << reg_succs_it->first << "\n";
                    for(size_t i = 0; i < reg_succs_it->second.size(); i++)
                        std::cout << "    To " << reg_succs_it->second[i].to()->to_string() << "\n";
                }
            }
            
            /* Try each register R */
            std::map< schedule_dep::reg_t, std::vector< schedule_dep > >::iterator reg_succs_it = reg_succs.begin();
            for(; reg_succs_it != reg_succs.end(); ++reg_succs_it)
            {
                std::vector< schedule_dep > reg_use = reg_succs_it->second;

                /* See of one successor S of U dominate all use
                 * NOTE: S can be any successor of U
                 * NOTE: there can be several dominators, we list tham all */
                std::vector< const schedule_unit * > dominators;
                
                for(size_t dom_idx = 0; dom_idx < succs.size(); dom_idx++)
                {
                    for(size_t i = 0; i < reg_use.size(); i++)
                        if(!path[name_map[succs[dom_idx].to()]][name_map[reg_use[i].to()]])
                            goto Lskip;
                    dominators.push_back(succs[dom_idx].to());
                    Lskip:
                    continue;
                }

                schedule_dep::reg_t cur_reg_it = reg_succs_it->first;
                schedule_dep::reg_t new_reg_id = reg_succs_it->first;
                bool new_id_generated = false;
                
                for(size_t dom_idx = 0; dom_idx < dominators.size(); dom_idx++)
                {
                    /* There is dominator D */
                    const schedule_unit *dom = dominators[dom_idx];

                    bool dominator_is_in_reg_use = false;
                    bool non_dominator_is_in_reg_use = false;
                    for(size_t i = 0; i < reg_use.size(); i++)
                        if(dom == reg_use[i].to())
                            dominator_is_in_reg_use = true;
                        else
                            non_dominator_is_in_reg_use = true;
                    /* There must a node in reg use which is not the dominator */
                    if(!non_dominator_is_in_reg_use)
                        continue; /* next dominator */

                    /*
                    std::cout << "Dominator: " << dom->to_string() << "\n";
                    for(size_t i = 0; i < reg_use.size(); i++)
                        std::cout << "  -> " << reg_use[i].to()->to_string() << "\n";
                    */ 

                    /* For each dependency (U,V) on register R, remove (U,V) */
                    dag.remove_dependencies(reg_use);
                    /* For each old dependency (U,V) on register R, add (D,V)
                     * NOTE: beware if the dominator is one of the considered child !
                     *       Otherwise, we'll create a self-loop */
                    for(size_t i = 0; i < reg_use.size(); i++)
                        if(dom != reg_use[i].to())
                        {
                            /* lazy generation to avoid wating generated registers */
                            if(!new_id_generated)
                            {
                                new_reg_id = dag.generate_unique_reg_id();
                                new_id_generated = true;
                            }
                            reg_use[i].set_reg(new_reg_id); /* use a new reg id if told */
                            reg_use[i].set_from(dom);
                        }

                    /* Add (U,D) on R
                     * Except if dominator_is_in_reg_use because the previous
                     * didn't modify the link so the (U,D) edge is already in the list */
                    if(!dominator_is_in_reg_use)
                        reg_use.push_back(schedule_dep(unit, dom,
                            schedule_dep::data_dep, cur_reg_it)); /* keep reg id here */

                    dag.add_dependencies(reg_use);
                    
                    goto Lgraph_changed;
                }
            }
        }

        /* Graph did not change */
        break;

        Lgraph_changed:
        graph_changed = true;
        continue;
    }

    status.set_modified_graph(graph_changed);
    status.set_deadlock(false);
    status.set_junction(false);

    s.schedule(dag, c);

    status.end_transformation();
}

}

