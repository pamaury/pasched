#include <sched-transform.hpp>
#include <sstream>
#include <stdexcept>
#include <cassert>
#include <algorithm>

namespace PAMAURY_SCHEDULER_NS
{

/**
 * schedule_dag_tranformation
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

glued_transformation_scheduler::glued_transformation_scheduler(const transformation *transform, const scheduler *sched)
    :m_transform(transform), m_scheduler(sched)
{
}

glued_transformation_scheduler::~glued_transformation_scheduler()
{
}

void glued_transformation_scheduler::schedule(schedule_dag& d, schedule_chain& c) const
{
    m_transform->transform(d, *m_scheduler, c);
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

void packed_transformation::transform(schedule_dag& d, const scheduler& s, schedule_chain& c) const
{
    glued_transformation_scheduler internal_sched(m_second, &s);
    m_first->transform(d, internal_sched, c);
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
    m_packers.push_back(new packed_transformation(m_packers.back(), transform));
}

void transformation_pipeline::transform(schedule_dag& d, const scheduler& s, schedule_chain& c) const
{
    if(m_packers.size() == 0)
        throw std::runtime_error("transformation_pipeline::transform called with empty pipeline");
    m_packers.back()->transform(d, s, c);
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

void unique_reg_ids::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c) const
{
    std::vector< schedule_dep > to_remove;
    std::vector< schedule_dep > to_add;
    unsigned reg_idx = 1;
    
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        const schedule_unit *unit = dag.get_units()[u];
        std::map< unsigned, unsigned > reg_map;
        
        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            schedule_dep dep = dag.get_succs(unit)[i];
            to_remove.push_back(dep);

            if(reg_map.find(dep.reg()) == reg_map.end())
                reg_map[dep.reg()] = reg_idx++;
            
            dep.set_reg(reg_map[dep.reg()]);
            to_add.push_back(dep);
        }
    }

    dag.remove_dependencies(to_remove);
    dag.add_dependencies(to_add);

    s.schedule(dag, c);
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

void strip_useless_order_deps::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c) const
{
    /* Shortcuts */
    const std::vector< const schedule_unit * >& units = dag.get_units();
    size_t n = units.size();
    /* Map a unit pointer to a number */
    std::map< const schedule_unit *, size_t > name_map;
    /* path[u][v] is true if there is a path from u to v */
    std::vector< std::vector< bool > > path;
    std::vector< schedule_dep > to_remove;

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

    s.schedule(dag, c);
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

void smart_fuse_two_units::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c) const
{
    /* Do two passes: first don't allow approx and then do so */
    bool allow_approx = false;
    std::vector< chain_schedule_unit * > fused;
    
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
        /* FIXME: commented for debug reasons */
        //delete fused[i];
    }
}

}

