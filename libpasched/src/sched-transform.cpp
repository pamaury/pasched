#include <sched-transform.hpp>
#include <tools.hpp>
#include <time-tools.hpp>
#include <sched-dag-viewer.hpp>
#include <sstream>
#include <stdexcept>
#include <cassert>
#include <algorithm>
#include <iostream>

#if 0
namespace PAMAURY_SCHEDULER_NS
{
    void check_red(const schedule_dag& dag)
    {
        for(size_t u = 0; u < dag.get_units().size(); u++)
        {
            const schedule_unit *unit = dag.get_units()[u];
            for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
            {
                const schedule_dep& dep = dag.get_succs(unit)[i];
                if(dep.kind() != schedule_dep::data_dep)
                    continue;
                for(size_t j = i + 1; j < dag.get_succs(unit).size(); j++)
                {
                    const schedule_dep& dep2 = dag.get_succs(unit)[j];
                    if(dep2.kind() != schedule_dep::data_dep)
                        continue;
                    if(dep.reg() == dep2.reg() && dep.to() == dep2.to())
                    {
                        debug_view_dag(dag);
                        throw std::runtime_error("redundant data dep");
                    }
                }
            }
        }
    }
}

#define DEBUG_CHECK_BEGIN_X(dag,chain) \
    std::vector< const schedule_unit * > __debug_units(dag.get_units()); \
    size_t __debug_old_size = chain.get_unit_count(); \
    check_red(dag);
#define DEBUG_CHECK_END_X(chain) \
    assert(chain.get_unit_count() == (__debug_old_size + __debug_units.size())); \
    for(size_t i = __debug_old_size; i < chain.get_unit_count(); i++) \
        assert(container_contains(__debug_units, chain.get_unit_at(i)));
#else
#define DEBUG_CHECK_BEGIN_X(dag, chain)
#define DEBUG_CHECK_END_X(chain)
#endif

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
    debug() << "---> glued_transformation_scheduler::schedule\n";
    m_transform->transform(d, *m_scheduler, c, m_status);
    debug() << "<--- glued_transformation_scheduler::schedule\n";
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
    /* don't do this because such a situation can happen if someone triggers an exception */
    /*
    if(m_level != 0)
        throw std::runtime_error("packed_status::~packed_status detected an invalid stacking level");
    */ 
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
    (void) d;
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
    debug() << "---> packed_transformation::transform\n";
    DEBUG_CHECK_BEGIN_X(d, c)
    
    packed_status ps(status);
    glued_transformation_scheduler internal_sched(m_second, &s, ps);
    m_first->transform(d, internal_sched, c, ps);
    
    DEBUG_CHECK_END_X(c)
    debug() << "<--- packed_transformation::transform\n";
}

void packed_transformation::set_transformation(bool first, const transformation *t)
{
    if(first)
        m_first = t;
    else
        m_second = t;
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
    debug() << "---> transformation_pipeline::transform\n";
    DEBUG_CHECK_BEGIN_X(d, c)
    
    if(m_pipeline.size() == 0)
    {
        /* act has a transparent transformation */
        status.begin_transformation();
        status.set_modified_graph(false);
        status.set_deadlock(false);
        status.set_junction(false);
        
        s.schedule(d, c);
        status.end_transformation();
    }
    else if(m_packers.size() > 0)
        m_packers.back()->transform(d, s, c, status);
    else
        m_pipeline[0]->transform(d, s, c, status);

    DEBUG_CHECK_END_X(c)
    debug() << "<--- transformation_pipeline::transform\n";
}

/**
 * auxillary_transformation_loop
 */
auxillary_transformation_loop::auxillary_transformation_loop(const transformation *x)
    :m_transform(x)
{
}

auxillary_transformation_loop::~auxillary_transformation_loop()
{
}

void auxillary_transformation_loop::transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    debug() << "---> auxillary_transformation_loop::transform\n";
    DEBUG_CHECK_BEGIN_X(d, c)
    
    if(!status.has_modified_graph() && !status.is_junction())
        s.schedule(d, c);
    else
    {
        /**
         * Important note:
         * we WANT to use a new/fresh status because this status will help us
         * determine if something changed. If we reached this point, it means
         * something changed, if we don't do that, we'll loop forever
         */
        basic_status bs;
        m_transform->transform(d, s, c, bs);
    }

    DEBUG_CHECK_END_X(c)
    debug() << "<--- auxillary_transformation_loop::transform\n";
}

void auxillary_transformation_loop::set_transformation(const transformation *t)
{
    m_transform = t;
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
    debug() << "---> transformation_loop::transform\n";
    DEBUG_CHECK_BEGIN_X(d, c)
    /**
     * Little hack here: we first build an auxillary object A which does the
     * following: it look at the status flags and if nothing changed then
     * execute the schedueler. Otherwise, it applies another trasnformation
     * given in parameter.
     *
     * Then we build a packer P which pack the transformation X with A.
     * At this point we have P=X;A
     *
     * Finally, we decide that the transformation A executes is P.
     * So we have something like:
     *
     * P=X;(if [not changed] then S else X;(if [not changed] ...))
     *
     */
    auxillary_transformation_loop atl(0);
    packed_transformation ps(m_transform, &atl);
    atl.set_transformation(&ps);

    ps.transform(d, s, c, status);

    DEBUG_CHECK_END_X(c)
    debug() << "<--- transformation_loop::transform\n";
}

/**
 * unique_reg_ids
 */
XTM_FW_DECLARE(unique_reg_ids)

unique_reg_ids::unique_reg_ids()
{
}

unique_reg_ids::~unique_reg_ids()
{
}

void unique_reg_ids::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    debug() << "---> unique_reg_ids::transform\n";
    DEBUG_CHECK_BEGIN_X(dag, c)
    
    std::vector< schedule_dep > to_remove;
    std::vector< schedule_dep > to_add;

    status.begin_transformation();

    XTM_FW_START(unique_reg_ids)

    std::map< schedule_dep::reg_t, schedule_dep::reg_t > phys_reg_map;
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        const schedule_unit *unit = dag.get_units()[u];
        std::map< schedule_dep::reg_t, schedule_dep::reg_t > reg_map;
        
        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            schedule_dep dep = dag.get_succs(unit)[i];
            to_remove.push_back(dep);
            /* Match both virtual and physical */
            if(dep.is_virt())
            {
                if(reg_map.find(dep.reg()) == reg_map.end())
                    reg_map[dep.reg()] = dag.generate_unique_reg_id();
            
                dep.set_reg(reg_map[dep.reg()]);
            }
            else if(dep.is_phys())
            {
                if(phys_reg_map.find(dep.reg()) == phys_reg_map.end())
                    phys_reg_map[dep.reg()] = dag.generate_unique_reg_id();
            
                dep.set_reg(phys_reg_map[dep.reg()]);
            }
            to_add.push_back(dep);
        }
    }

    dag.remove_dependencies(to_remove);
    dag.add_dependencies(to_add);

    XTM_FW_STOP(unique_reg_ids)

    status.set_modified_graph(true);
    status.set_deadlock(false);
    status.set_junction(false);

    s.schedule(dag, c);

    status.end_transformation();

    DEBUG_CHECK_END_X(c)
    debug() << "<--- unique_reg_ids::transform\n";
}

/**
 * strip_useless_order_deps
 */
XTM_FW_DECLARE(strip_useless_order_deps)

strip_useless_order_deps::strip_useless_order_deps()
{
}

strip_useless_order_deps::~strip_useless_order_deps()
{
}

void strip_useless_order_deps::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    debug() << "---> strip_useless_order_deps::transform\n";
    DEBUG_CHECK_BEGIN_X(dag, c)
    /* Shortcuts */
    const std::vector< const schedule_unit * >& units = dag.get_units();
    size_t n = units.size();
    /* Map a unit pointer to a number */
    std::map< const schedule_unit *, size_t > name_map;
    /* path[u][v] is true if there is a path from u to v */
    std::vector< std::vector< bool > > path;
    std::vector< schedule_dep > to_remove;

    status.begin_transformation();
    XTM_FW_START(strip_useless_order_deps)

    /* As a first step, remove duplicate order, that is when
     * there are several order deps between same pair of units */
    for(size_t u = 0; u < n; u++)
    {
        const schedule_unit *unit = units[u];
        const std::vector< schedule_dep >& succs = dag.get_succs(unit);
        std::map< const schedule_unit *, size_t > order_count;
        for(size_t i = 0; i < succs.size(); i++)
            if(succs[i].kind() == schedule_dep::order_dep)
            {
                if((++order_count[succs[i].to()]) >= 2)
                    to_remove.push_back(succs[i]);
            }
    }

    dag.remove_dependencies(to_remove);
    to_remove.clear();

    dag.build_path_map(path, name_map);

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

    XTM_FW_STOP(strip_useless_order_deps)

    status.set_modified_graph(to_remove.size() > 0);
    status.set_deadlock(false);
    status.set_junction(false);

    s.schedule(dag, c);

    status.end_transformation();

    DEBUG_CHECK_END_X(c)
    debug() << "<--- strip_useless_order_deps::transform\n";
}

/**
 * smart_fuse_two_units
 */
XTM_FW_DECLARE(smart_fuse_two_units)
XTM_BW_DECLARE(smart_fuse_two_units)

smart_fuse_two_units::smart_fuse_two_units(bool allow_non_optimal_irp_calculation,
    bool allow_weak)
    :m_allow_non_optimal_irp_calculation(allow_non_optimal_irp_calculation),
     m_allow_weak_fusing(allow_weak)
{
}

smart_fuse_two_units::~smart_fuse_two_units()
{
}

void smart_fuse_two_units::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    debug() << "---> smart_fuse_two_units::transform\n";
    DEBUG_CHECK_BEGIN_X(dag, c)
    /* Do two passes: first don't allow approx and then do so */
    bool allow_approx = false;
    bool modified = false;
    std::vector< chain_schedule_unit * > fused;

    status.begin_transformation();
    XTM_FW_START(smart_fuse_two_units)
    
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
            //*
            //debug() << "Unit: " << unit->to_string() << "\n";
            //debug() << "  VC=" << vc << "\n";
            //debug() << "  VU=" << vu << "\n";
            //debug() << "  VD=" << vd << "\n";
            //*/
            
            /* Case 1
             * - unit has one predecessor only
             * - unit destroys more variable than it creates ones
             * - IRP of unit is lower than the number of destroyed variables
             * Then
             * - fuse unit to predecessor */
            /*
            if(ipreds.size() == 1)
            {
                debug() << "Unit: " << unit->to_string() << "\n";
                debug() << "VC=" << vc.size() << "\n";
                debug() << "VD=" << vd.size() << "\n";
                debug() << "VU=" << vu.size() << "\n";
            }
            */
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
                if(c == 0 && m_allow_weak_fusing)
                {
                    if(weak_fuse(dag, unit, dag.get_succs(unit)[0].to()))
                        goto Lgraph_changed;
                }
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
        modified = true;
    }

    XTM_FW_STOP(smart_fuse_two_units)

    status.set_modified_graph(modified);
    status.set_deadlock(false);
    status.set_junction(false);

    /* schedule DAG */
    s.schedule(dag, c);

    XTM_BW_START(smart_fuse_two_units)
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

        c.expand_unit_at(j, fused[i]->get_chain());
        delete fused[i];
    }

    XTM_BW_STOP(smart_fuse_two_units)

    status.end_transformation();

    DEBUG_CHECK_END_X(c)
    debug() << "<--- smart_fuse_two_units::transform\n";
}

bool smart_fuse_two_units::weak_fuse(schedule_dag& dag, const schedule_unit *a, const schedule_unit *b) const
{
    /* For each dep (U,B) add a dep (U,A) */
    std::vector< schedule_dep > to_add;
    for(size_t i = 0; i < dag.get_preds(b).size(); i++)
    {
        const schedule_unit *unit = dag.get_preds(b)[i].from();
        if(unit == a)
            continue;
        std::set< const schedule_unit * > reach = dag.get_reachable(unit, schedule_dag::rf_follow_succs);

        if(reach.find(a) == reach.end())
            to_add.push_back(schedule_dep(unit, a, schedule_dep::order_dep));
    }
    
    dag.add_dependencies(to_add);
    return to_add.size() > 0;
}

/**
 * simplify_order_cuts
 */
XTM_FW_DECLARE(simplify_order_cuts)

simplify_order_cuts::simplify_order_cuts()
{
}

simplify_order_cuts::~simplify_order_cuts()
{
}

void simplify_order_cuts::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    DEBUG_CHECK_BEGIN_X(dag, c)
    XTM_FW_START(simplify_order_cuts)
    do_transform(dag, s, c, status, 0);
    XTM_FW_STOP(simplify_order_cuts)
    DEBUG_CHECK_END_X(c)
}

void simplify_order_cuts::do_transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status, int level) const
{
    if(level == 0)
    {
        debug() << "---> simplify_order_cuts::transform\n";
        status.begin_transformation();
    }
    
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
            break;

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

        if(level == 0)
        {
            status.end_transformation();
            debug() << "<--- simplify_order_cuts::transform\n";
        }
        return;
    }

    if(level == 0)
    {
        status.set_modified_graph(false);
        status.set_deadlock(false);
        status.set_junction(false);
    }

    XTM_FW_STOP(simplify_order_cuts)
    /* otherwise, schedule the whole graph */
    s.schedule(dag, c);

    XTM_FW_START(simplify_order_cuts)

    if(level == 0)
    {
        status.end_transformation();
        debug() << "<--- simplify_order_cuts::transform\n";
    }
}

/**
 * split_def_use_dom_use_deps
 */
XTM_FW_DECLARE(split_def_use_dom_use_deps)
XTM_BW_DECLARE(split_def_use_dom_use_deps)

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
    debug() << "---> split_def_use_dom_use_deps::transform\n";
    DEBUG_CHECK_BEGIN_X(dag, c)
    XTM_FW_START(split_def_use_dom_use_deps)

    /* Map a unit pointer to a number */
    std::map< const schedule_unit *, size_t > name_map;
    /* path[u][v] is true if there is a path from u to v */
    std::vector< std::vector< bool > > path;
    /* First compute path map, it will not changed during the algorithm
     * even though some edges are changed */
    dag.build_path_map(path, name_map);
    /* Chain units added (each one "contains" only one unit but we need to tweak IRP) */
    std::vector< chain_schedule_unit * > chains_added;
    
    status.begin_transformation();
    debug() << "start\n";

    /* Each iteration might modify the graph */
    while(true)
    {
        bool quit = true;
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
                /* Only consider virtual deps, physical registers are too tricky */
                if(succs[i].is_virt())
                    reg_succs[succs[i].reg()].push_back(succs[i]);
            
            /* Try each register R */
            std::map< schedule_dep::reg_t, std::vector< schedule_dep > >::iterator reg_succs_it = reg_succs.begin();
            for(; reg_succs_it != reg_succs.end(); ++reg_succs_it)
            {
                std::vector< schedule_dep > reg_use = reg_succs_it->second;

                /* See if one successor S of U dominate all uses
                 * NOTE: S can be any successor of U
                 * NOTE: there can be several dominators, we list them all */
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
                            schedule_dep::virt_dep, cur_reg_it)); /* keep reg id here */

                    dag.add_dependencies(reg_use);

                    /* Create a new unit to replace the old one */
                    chain_schedule_unit *scu = new chain_schedule_unit;
                    scu->get_chain().push_back(dom);
                    /* IRP is computed later */

                    /* warning here: replace_unit would break units order, that's why we delay
                     * the replacement to after the loop */
                    chains_added.push_back(scu);

                    /* continue to next register but ask for a new general round
                     * because we can continue splitting on the dominator perhaps */
                    quit = false;
                    break;
                }
            }
        }

        if(quit)
            /* Graph did not change */
            break;
    }

    /* now do the replacement, using a map to forward changes if any */
    std::map< const schedule_unit *, const schedule_unit * > forward_map;
    for(size_t i = 0; i < chains_added.size(); i++)
    {
        assert(chains_added[i]->get_chain().size() == 1);
        const schedule_unit *old = chains_added[i]->get_chain()[0];
        /* forward */
        while(forward_map.find(old) != forward_map.end())
            old = forward_map[old];
        /* update chain */
        chains_added[i]->get_chain()[0] = old;
        /* compute IRP */
        chains_added[i]->set_internal_register_pressure(old->internal_register_pressure() + 1);
        /* update map */
        forward_map[old] = chains_added[i];
        /* do replacement */
        dag.replace_unit(old, chains_added[i]);
    }

    XTM_FW_STOP(split_def_use_dom_use_deps)

    status.set_modified_graph(chains_added.size() > 0);
    status.set_deadlock(false);
    status.set_junction(false);

    s.schedule(dag, c);

    XTM_BW_START(split_def_use_dom_use_deps)
    /* replace back units */
    std::reverse(chains_added.begin(), chains_added.end());
    /* exploit the fact that each replacement don't change indices because
     * we replace one unit by another */
    std::map< const schedule_unit *, size_t > index_map;
    for(size_t j = 0; j < c.get_unit_count(); j++)
        index_map[c.get_unit_at(j)] = j;

    for(size_t i = 0; i < chains_added.size(); i++)
    {
        if(index_map.find(chains_added[i]) == index_map.end())
            throw std::runtime_error("split_def_use_dom_use_deps::transform detected inconsistent schedule chain");
        size_t j = index_map[chains_added[i]];

        c.expand_unit_at(j, chains_added[i]->get_chain());
        assert(chains_added[i]->get_chain().size() == 1 && "split_def_use_dom_use_deps::transform has strange chain");
        index_map.erase(chains_added[i]);
        index_map[chains_added[i]->get_chain()[0]] = j;
        delete chains_added[i];
    }

    XTM_BW_STOP(split_def_use_dom_use_deps)

    status.end_transformation();

    DEBUG_CHECK_END_X(c)
    debug() << "<--- split_def_use_dom_use_deps::transform\n";
}

/**
 * break_symmetrical_branch_merge
 */
XTM_FW_DECLARE(break_symmetrical_branch_merge)

break_symmetrical_branch_merge::break_symmetrical_branch_merge()
{
}

break_symmetrical_branch_merge::~break_symmetrical_branch_merge()
{
}

void break_symmetrical_branch_merge::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    debug() << "---> break_symmetrical_branch_merge::transform\n";
    DEBUG_CHECK_BEGIN_X(dag, c)
    
    std::vector< schedule_dep > to_add;

    status.begin_transformation();
    XTM_FW_START(break_symmetrical_branch_merge)
    
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        #if 0
        const schedule_unit *unit = dag.get_units()[u];
        std::set< const schedule_unit * > preds_order_succs;
        std::set< const schedule_unit * > preds_preds;
        unsigned irp = 0;
        std::set< const schedule_unit * > preds = dag.get_reachable(unit,
            schedule_dag::rf_follow_preds_data | schedule_dag::rf_immediate);

        //debug() << "  Unit: " << unit->to_string() << "\n";

        if(preds.size() < 2)
            continue;
        if(dag.get_reachable(unit, schedule_dag::rf_follow_preds_order | schedule_dag::rf_immediate).size() != 0)
            continue;
        for(std::set< const schedule_unit * >::iterator it = preds.begin(); it != preds.end(); ++it)
        {
            const schedule_unit *the_pred = *it;
            //debug() << "    Pred: " << the_pred->to_string() << "\n";
            /* - must be single data successor */
            if(dag.get_reachable(the_pred, schedule_dag::rf_follow_succs_data | schedule_dag::rf_immediate).size() != 1)
                goto Lnext;
            /* - predecessors's predecessors */
            std::set< const schedule_unit * > this_preds_order_succs = dag.get_reachable(the_pred,
                schedule_dag::rf_follow_succs_order | schedule_dag::rf_immediate);
            std::set< const schedule_unit * > this_preds_preds = dag.get_reachable(the_pred,
                schedule_dag::rf_follow_preds | schedule_dag::rf_immediate);
            
            if(it == preds.begin())
            {
                preds_preds = this_preds_preds;
                preds_order_succs = this_preds_order_succs;
                irp = the_pred->internal_register_pressure();
            }
            else
            {
                if(preds_preds != this_preds_preds)
                    goto Lnext;
                if(preds_order_succs != this_preds_order_succs)
                    goto Lnext;
                if(irp != the_pred->internal_register_pressure())
                    goto Lnext;
            }
        }

        for(std::set< const schedule_unit * >::iterator it = preds.begin(); it != preds.end(); ++it)
        {
            std::set< const schedule_unit * >::iterator it2 = it;
            ++it2;
            if(it2 == preds.end())
                break;

            schedule_dep d;
            d.set_from(*it);
            d.set_to(*it2);
            d.set_kind(schedule_dep::order_dep);
            to_add.push_back(d);
        }
        #endif

        /**
         * Really simple and special case of symmetrical situation:
         * One dominator node U
         * Which has a subset V_1, ..., V_n of its children
         * One collector node W
         * With conditions:
         * 1) Each child V_i must only have W as successor and U as predecessor
         * 2) The number of data edges between U and V_i must not depend on i (constant)
         * 3) Each register created by U must
         *    a) either be used by all V_i
         *    b) either be used by only one V_i
         * 4) The number of register created by V_i must not depend on i (constant)
         */
        const schedule_unit *unit = dag.get_units()[u];
        std::set< const schedule_unit * > succs =
            dag.get_reachable(unit, schedule_dag::rf_follow_succs | schedule_dag::rf_immediate);
        std::set< const schedule_unit * >::iterator it;

        /* filter out nodes with several predecessors and/or successors */
        /* compute set of successors' successors at the same time */
        std::map< const schedule_unit *, std::set< const schedule_unit * > > groups;
        std::map< const schedule_unit *, std::set< const schedule_unit * > >::iterator git;
        {
            std::vector< const schedule_unit * > to_remove;
            for(it = succs.begin(); it != succs.end(); ++it)
            {
                if(dag.get_reachable(*it, schedule_dag::rf_follow_preds | schedule_dag::rf_immediate).size() != 1)
                {
                    to_remove.push_back(*it);
                    continue;
                }
                if(dag.get_reachable(*it, schedule_dag::rf_follow_succs | schedule_dag::rf_immediate).size() != 1)
                {
                    to_remove.push_back(*it);
                    continue;
                }

                /* pick the first successor because they are all the same */
                groups[dag.get_succs(*it)[0].to()].insert(*it);
            }
            for(size_t i = 0; i < to_remove.size(); i++)
                succs.erase(to_remove[i]);
        }
        /* check if it still worth it */
        if(succs.size() < 2)
            goto Lnext;

        /* process each group */
        for(git = groups.begin(); git != groups.end(); ++git)
        {
            std::set< const schedule_unit * > grp = git->second;
            const schedule_unit *collector = git->first;
            /* group must not be trivial */
            if(grp.size() <= 1)
                continue;
            /* Check conditions 2) and 4) */
            size_t nb_input_regs = 0;
            size_t nb_ouput_regs = 0;
            for(it = grp.begin(); it != grp.end(); ++it)
            {
                size_t cnt = dag.get_reg_use(*it).size();
                if(it == grp.begin())
                    nb_input_regs = cnt;
                else if(nb_input_regs != cnt)
                    goto Lnext_group;

                cnt = dag.get_reg_create(*it).size();
                if(it == grp.begin())
                    nb_ouput_regs = cnt;
                else if(nb_ouput_regs != cnt)
                    goto Lnext_group;
            }
            /* Check condition 3) */
            {
                std::map< schedule_dep::reg_t, size_t > reg_use_count;
                std::map< schedule_dep::reg_t, size_t >::iterator ruc_it;
                for(it = grp.begin(); it != grp.end(); ++it)
                {
                    std::set< schedule_dep::reg_t > use = dag.get_reg_use(*it);
                    std::set< schedule_dep::reg_t >::iterator rit;
                    for(rit = use.begin(); rit != use.end(); ++rit)
                        reg_use_count[*rit]++;
                }

                for(ruc_it = reg_use_count.begin(); ruc_it != reg_use_count.end(); ++ruc_it)
                {
                    /* Number of use must either be 1 or grp.size() */
                    if(ruc_it->second != 1 && ruc_it->second != grp.size())
                        goto Lnext_group;
                }
            }

            debug() << "Found group:\n";
            debug() << "  Dominator: " << unit->to_string() << "\n";
            for(it = grp.begin(); it != grp.end(); ++it)
                debug() << "    Node: " << (*it)->to_string() << "\n";
            debug() << "  Collector: " << collector->to_string() << "\n";

            /* Add order deps */
            {
                const schedule_unit *last = 0;
                for(it = grp.begin(); it != grp.end(); ++it)
                {
                    if(last != 0)
                        to_add.push_back(schedule_dep(last, *it, schedule_dep::order_dep));
                    last = *it;
                }
            }

            Lnext_group:
            continue;
        }
        

        Lnext:
        continue;
    }

    dag.add_dependencies(to_add);

    XTM_FW_STOP(break_symmetrical_branch_merge)

    status.set_modified_graph(to_add.size() > 0);
    status.set_deadlock(false);
    status.set_junction(false);

    s.schedule(dag, c);
    

    status.end_transformation();

    DEBUG_CHECK_END_X(c)
    debug() << "<--- break_symmetrical_branch_merge::transform\n";
}

/**
 * collapse_chains
 */
XTM_FW_DECLARE(collapse_chains)
XTM_BW_DECLARE(collapse_chains)

collapse_chains::collapse_chains()
{
}

collapse_chains::~collapse_chains()
{
}

void collapse_chains::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    debug() << "---> collapse_chains::transform\n";
    DEBUG_CHECK_BEGIN_X(dag, c)
    
    status.begin_transformation();
    
    /* Check if the graph is a chain */
    if(dag.get_units().size() <= 1 || dag.get_roots().size() != 1)
        goto Lnot_chain;
    
    {
        XTM_FW_START(collapse_chains)
        
        std::vector< const schedule_unit * > chain;
        const schedule_unit *unit = dag.get_roots()[0];
        unsigned irp = 0;
        
        while(true)
        {
            chain.push_back(unit);
            size_t s = dag.get_reachable(unit, schedule_dag::rf_follow_succs |
                        schedule_dag::rf_immediate).size();
            irp = std::max(irp, unit->internal_register_pressure());
            if(s == 0)
                break;
            if(s != 1)
                goto Lnot_chain;

            irp = std::max(irp, (unsigned)dag.get_reg_create(unit).size());
            
            unit = dag.get_succs(unit)[0].to();
        }

        if(chain.size() != dag.get_units().size())
            goto Lnot_chain;
        #if 1
        /* no schedule */
        status.set_modified_graph(false);
        status.set_junction(false);
        status.set_deadlock(true);

        c.insert_units_at(c.get_unit_count(), chain);

        #else
        chain_schedule_unit scu;
        scu.get_chain() = chain;
        scu.set_internal_register_pressure(irp);

        dag.clear();
        dag.add_unit(&scu);

        status.set_modified_graph(false);
        status.set_junction(false);
        status.set_deadlock(false);

        XTM_FW_STOP(collapse_chains)

        s.schedule(dag, c);

        XTM_BW_START(collapse_chains)

        for(size_t i = 0; i < c.get_unit_count(); i++)
        {
            if(c.get_unit_at(i) == &scu)
            {
                c.expand_unit_at(i, chain);
                break;
            }
        }

        XTM_BW_STOP(collapse_chains)

        #endif
    }

    /* return */
    goto Lret;

    Lnot_chain:
    /* normal schedule */
    status.set_modified_graph(false);
    status.set_junction(false);
    status.set_deadlock(false);
    s.schedule(dag, c);

    Lret:
    status.end_transformation();

    DEBUG_CHECK_END_X(c)
    debug() << "<--- collapse_chains::transform\n";
}

/**
 * split_merge_branch_units
 */
XTM_FW_DECLARE(split_merge_branch_units)

split_merge_branch_units::split_merge_branch_units()
{
}

split_merge_branch_units::~split_merge_branch_units()
{
}

void split_merge_branch_units::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    DEBUG_CHECK_BEGIN_X(dag, c)
    XTM_FW_START(split_merge_branch_units)
    do_transform(dag, s, c, status, 0);
    XTM_FW_STOP(split_merge_branch_units)
    DEBUG_CHECK_END_X(c)
}

void split_merge_branch_units::do_transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status, int level) const
{
    if(level == 0)
    {
        debug() << "---> split_merge_branch_units::transform\n";
        status.begin_transformation();
    }

    std::vector< const schedule_unit * > to_split;

    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        const schedule_unit *unit = dag.get_units()[u];
        std::set< const schedule_unit * > pr = dag.get_reachable(unit, schedule_dag::rf_follow_preds);
        std::set< const schedule_unit * > sr = dag.get_reachable(unit, schedule_dag::rf_follow_succs);

        if(pr.size() == 0 || sr.size() == 0)
            continue;
        if((sr.size() + pr.size() + 1) != dag.get_units().size())
            continue;

        std::set< const schedule_unit * >::iterator it = pr.begin();

        for(; it != pr.end(); ++it)
        {
            for(size_t i = 0; i < dag.get_succs(*it).size(); i++)
                if(sr.find(dag.get_succs(*it)[i].to()) != sr.end())
                    goto Lskip;
        }

        it = sr.begin();
        for(; it != sr.end(); ++it)
        {
            for(size_t i = 0; i < dag.get_preds(*it).size(); i++)
                if(pr.find(dag.get_preds(*it)[i].from()) != pr.end())
                    goto Lskip;
        }

        //std::cout << "found split node: " << dag.get_units()[u]->to_string() << "\n";
        //std::cout << "pr: " << pr << "\n";
        //std::cout << "sr: " << sr << "\n";

        /* split here */
        {
            schedule_dag *cpy = dag.dup();

            dag.remove_units(set_to_vector(sr));
            cpy->remove_units(set_to_vector(pr));

            if(level == 0)
            {
                status.set_modified_graph(true);
                status.set_junction(true);
                status.set_deadlock(false);
            }
            
            XTM_FW_STOP(split_merge_branch_units)

            s.schedule(dag, c);

            XTM_FW_START(split_merge_branch_units)
            
            if(c.get_unit_count() == 0 || c.get_unit_at(c.get_unit_count() - 1) != unit)
                throw std::runtime_error("split_merge_branch_units::do_transform detected a bad schedule");
            c.remove_unit_at(c.get_unit_count() - 1);

            s.schedule(*cpy, c);
            delete cpy;

            return;
        }

        Lskip:
        continue;
    }

    if(level == 0)
    {
        status.set_modified_graph(false);
        status.set_junction(false);
        status.set_deadlock(false);
    }

    XTM_FW_STOP(split_merge_branch_units)

    s.schedule(dag, c);

    XTM_FW_START(split_merge_branch_units)

    if(level == 0)
    {
        status.end_transformation();
        debug() << "<--- split_merge_branch_units::transform\n";
    }
}

/**
 * strip_dataless_units
 */
XTM_FW_DECLARE(strip_dataless_units)
XTM_BW_DECLARE(strip_dataless_units)

strip_dataless_units::strip_dataless_units()
{
}

strip_dataless_units::~strip_dataless_units()
{
}

void strip_dataless_units::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    debug() << "---> strip_dataless_units::transform\n";
    DEBUG_CHECK_BEGIN_X(dag, c)
    
    status.begin_transformation();

    std::vector< std::pair< const schedule_unit *,
                    std::pair< std::vector< const schedule_unit * >,
                               std::vector< const schedule_unit * > > > > stripped;

    XTM_FW_START(strip_dataless_units)
    /* We repeat everything from the beginning at each step because
     * each modification can add order dependencies and delete a node,
     * so it's easier this way */
    while(true)
    {
        /* avoid trivial unit case */
        if(dag.get_units().size() <= 1)
            break;
        /* Find such a node */
        size_t u;
        const schedule_unit *unit = 0;
        for(u = 0; u < dag.get_units().size(); u++)
        {
            unit = dag.get_units()[u];
            /* - costless */
            if(unit->internal_register_pressure() != 0)
                goto Lskip;
            /* - no data in */
            for(size_t i = 0; i < dag.get_preds(unit).size(); i++)
                if(dag.get_preds(unit)[i].kind() != schedule_dep::order_dep)
                    goto Lskip;

            /* - no data out */
            for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
                if(dag.get_succs(unit)[i].kind() != schedule_dep::order_dep)
                    goto Lskip;
            break;
            Lskip:
            continue;
        }

        /* Stop if not found */
        if(u == dag.get_units().size())
            break;
        /* We will remove the unit but add some dependencies to keep correctness
         * We add the dependencies at the end because it can invalidate
         * the lists we are going through */
        std::vector< schedule_dep > to_add;
        
        for(size_t i = 0; i < dag.get_preds(unit).size(); i++)
            for(size_t j = 0; j < dag.get_succs(unit).size(); j++)
            {
                schedule_dep d;
                d.set_from(dag.get_preds(unit)[i].from());
                d.set_to(dag.get_succs(unit)[j].to());
                d.set_kind(schedule_dep::order_dep);

                to_add.push_back(d);
            }

        stripped.push_back(
            std::make_pair(unit,
            std::make_pair(
                set_to_vector(dag.get_reachable(unit,
                    schedule_dag::rf_follow_succs | schedule_dag::rf_immediate)),
                set_to_vector(dag.get_reachable(unit,
                    schedule_dag::rf_follow_preds | schedule_dag::rf_immediate)))));
        /* Remove the unit */
        dag.remove_unit(unit);
        /* Add the dependencies */
        dag.add_dependencies(to_add);
    }

    XTM_FW_STOP(strip_dataless_units)

    status.set_modified_graph(stripped.size() > 0);
    status.set_deadlock(false);
    status.set_junction(false);
    s.schedule(dag, c);

    XTM_BW_START(strip_dataless_units)

    std::reverse(stripped.begin(), stripped.end());

    for(size_t i = 0; i < stripped.size(); i++)
    {
        const schedule_unit *inst = stripped[i].first;
        const std::vector< const schedule_unit * > succs = stripped[i].second.first;
        const std::vector< const schedule_unit * > preds = stripped[i].second.second;
        size_t real_pos;

        if(succs.size() > 0)
        {
            real_pos = c.get_unit_count();
            for(size_t pos = 0; pos < c.get_unit_count(); pos++)
            {
                if(container_contains(succs, c.get_unit_at(pos)))
                {
                    real_pos = pos;
                    break;
                }
            }
            if(real_pos == c.get_unit_count())
                throw std::runtime_error("strip_dataless_units::transform detected incomplete schedule");
        }
        else if(preds.size() > 0)
        {
            
            real_pos = 0;
            for(int pos = c.get_unit_count() - 1; pos >= 0; pos--)
            {
                if(container_contains(preds, c.get_unit_at(pos)))
                {
                    real_pos = pos + 1;
                    break;
                }
            }
            if(real_pos == 0)
                throw std::runtime_error("strip_dataless_units::transform detected incomplete schedule");
        }
        else
        {
            /* we chan choose any place */
            real_pos = c.get_unit_count();
        }

        c.insert_unit_at(real_pos, inst);
    }

    XTM_BW_STOP(strip_dataless_units)

    status.end_transformation();

    DEBUG_CHECK_END_X(c)
    debug() << "<--- strip_dataless_units::transform\n";
}

/**
 * handle_physical_regs
 */
XTM_FW_DECLARE(handle_physical_regs)

handle_physical_regs::handle_physical_regs(bool promote_phys_to_virt)
    : m_promote(promote_phys_to_virt)
{
}

handle_physical_regs::~handle_physical_regs()
{
}

void handle_physical_regs::transform(schedule_dag& dag, const scheduler& s, schedule_chain& c,
    transformation_status& status) const
{
    typedef std::vector< std::pair< const schedule_unit *, const schedule_unit * > > reg_problem_list;
    typedef std::map< schedule_dep::reg_t, reg_problem_list > reg_problem_map;
    typedef std::map< schedule_dep::reg_t, std::set< const schedule_unit * > > reg_creator_list;
    bool modified = false;
    reg_problem_map problems;

    debug() << "---> handle_physical_regs::transform\n";
    XTM_FW_START(handle_physical_regs)

    /**
     * This transformation is organized as follow:
     * 1) build a map of physical registers creators
     * 2) skip work if no register has no more than one creator
     * 3) build an interferance map of creators and use a path map of the graph
     *    to reduce it
     * 4) skip work if there is no actual problem
     * 5) for each problem, check if there is a natural partial order between creators
     *    then can be strengthened to solve it and do it
     * 6) skip any further work if the problem list if empty
     * 7) ...
     *
     * Note that the checks are more expensive each time because there are actually only
     * a few relevant cases so we try to minimize the work in most cases */
    
    status.begin_transformation();

    /* build a map of physical registers creators */
    reg_creator_list reg_creators;
    std::vector< schedule_dep::reg_t > several_creators;
    for(size_t i = 0; i < dag.get_deps().size(); i++)
        if(dag.get_deps()[i].is_phys())
        {
            reg_creators[dag.get_deps()[i].reg()].insert(dag.get_deps()[i].from());
            if(reg_creators[dag.get_deps()[i].reg()].size() == 2)
                several_creators.push_back(dag.get_deps()[i].reg());
        }
    
    /* If no physical register has several creators, just promote them all and go on */
    if(several_creators.size() == 0)
        goto Lpromote_all;

    /* We are here because at least one physical register used twice or more.
     * Hopefully, in a number of cases, this is a false positive because the
     * natural ordering will prevent them from interfering.
     * Check this for each register.
     * Loop because we add edges for detected partial order which can change things */
    while(true)
    {
        reg_problem_map partials;
        /* create path map */
        std::vector< std::vector< bool > > path;
        std::map< const schedule_unit *, size_t> name_map;
        dag.build_path_map(path, name_map);
        
        for(size_t i = 0; i < several_creators.size(); i++)
        {
            schedule_dep::reg_t reg = several_creators[i];
            std::vector< const schedule_unit * > creators = set_to_vector(reg_creators[reg]);
            
            /* Create interferance map */
            /* C interfere with C' if it's possible to schedule C, C' and their use such has the
             * same physical register is alive and created by both C and C' at the same time
             *
             * C has partial order with C' if the dependencies suggest that C and its uses must be
             * schedule before C' but we need to add more dependencies to enforce that */
            std::vector< std::vector< bool > > interfere;
            std::vector< std::vector< bool > > partial_order;
            interfere.resize(creators.size());
            partial_order.resize(creators.size());
            for(size_t i = 0; i < interfere.size(); i++)
            {
                interfere[i].resize(creators.size(), true); /* interfere unless proven the contrary */
                partial_order[i].resize(creators.size());
                interfere[i][i] = false;
            }

            /* this list does not consider symmetry because if C must happen before D
             * then obviously D happens before C so in one case, we can't conclude that
             * they don't interfere ! */
            for(size_t i = 0; i < creators.size(); i++)
            {
                /* consider a creator C */
                const schedule_unit *creator = creators[i];
                /* consider every other creator D */
                for(size_t j = 0; j < creators.size(); j++)
                {
                    if(i == j)
                        continue;
                    const schedule_unit *other = creators[j];
                    /* now to prove that C does not intefere with D, we must ensure that
                     * any use of the physical register created by C has a path to D
                     * So consider every succ S of C with physical reg dep */
                    bool inter = false;
                    bool partial = false;
                    for(size_t k = 0; k < dag.get_succs(other).size(); k++)
                    {
                        const schedule_dep& dep = dag.get_succs(other)[k];
                        if(!dep.is_phys() || dep.reg() != reg)
                            continue;
                        const schedule_unit *use = dep.to();
                        /* if there is no path from S to D, then they might interfere */
                        if(!path[name_map[use]][name_map[creator]])
                            inter = true;
                        /* if there is a path from C to S, then it means that C has a partial order
                         * with S */
                        if(creator != use && path[name_map[creator]][name_map[use]])
                            partial = true;
                    }
                    /* we proved they do not interfere */
                    if(!inter)
                    {
                        interfere[i][j] = false;
                        interfere[j][i] = false;
                    }
                    if(partial)
                        partial_order[i][j] = true;
                }
            }

            /* Make a summary of interferance pairs */
            reg_problem_list problem_list;
            reg_problem_list partial_list;
            /* the map must be symmetrical at this point */
            for(size_t i = 0; i < creators.size(); i++)
                for(size_t j = i + 1; j < creators.size(); j++)
                    if(interfere[i][j])
                    {
                        /* check consistency */
                        if(partial_order[i][j] && partial_order[j][i])
                        {
                            debug_view_dag(dag);
                            throw std::runtime_error("graph is not schedulable because of phys regs");
                        }
                        if(partial_order[i][j])
                            partial_list.push_back(std::make_pair(creators[i], creators[j]));
                        else if(partial_order[j][i])
                            partial_list.push_back(std::make_pair(creators[j], creators[i]));
                        else
                            problem_list.push_back(std::make_pair(creators[i], creators[j]));
                        
                    }

            if(problem_list.size() != 0)
                problems[reg] = problem_list;
            if(partial_list.size() != 0)
                partials[reg] = partial_list;
        }

        if(partials.size() == 0)
            break;
        /* Enforce partial orders */
        for(reg_problem_map::iterator it = partials.begin(); it != partials.end(); ++it)
        {
            reg_problem_list& l = it->second;
            for(size_t i = 0; i < l.size(); i++)
            {
                const schedule_unit *c_from = l[i].first;
                const schedule_unit *c_to = l[i].second;
                /* for each successor S of C_from on register R, add a dependency
                 * from S to C_to */
                for(size_t i = 0; i < dag.get_succs(c_from).size(); i++)
                {
                    const schedule_dep& dep = dag.get_succs(c_from)[i];
                    if(dep.is_phys() && dep.reg() == it->first &&
                            !path[name_map[dep.to()]][name_map[c_to]])
                    {
                        dag.add_dependency(
                            schedule_dep(
                                dep.to(), c_to, schedule_dep::order_dep));
                    }
                }
            }
        }
        /* continue the loop */
    }
    /* If there are no more problem, just promote them all */
    if(problems.size() == 0)
        goto Lpromote_all;
    /* Ok, do a partial promoting of all creators which are not in the problem list */
    {
        /* list all (reg,creators) pairs in problem list */
        std::set< std::pair< schedule_dep::reg_t, const schedule_unit * > > problematic_pairs;
        for(reg_problem_map::iterator it = problems.begin(); it != problems.end(); ++it)
        {
            reg_problem_list& l = it->second;

            for(size_t i = 0; i < l.size(); i++)
            {
                problematic_pairs.insert(std::make_pair(it->first, l[i].first));
                problematic_pairs.insert(std::make_pair(it->first, l[i].second));
            }
        }
        /* list all creators and promote them if there are not in a problematic pair */
        for(reg_creator_list::iterator it = reg_creators.begin(); it != reg_creators.end(); ++it)
        {
            std::set< const schedule_unit * >::iterator it2;
            for(it2 = it->second.begin(); it2 != it->second.end(); ++it2)
                if(problematic_pairs.find(std::make_pair(it->first, *it2)) == problematic_pairs.end())
                    promote_phys_register(dag, *it2, it->first, modified);
        }
    }
    
    /* display */
    /*
    debug() << "Problem list:\n";
    for(reg_problem_map::iterator it = problems.begin(); it != problems.end(); ++it)
    {
        debug() << "  Register " << it->first << "\n";
        reg_problem_list& l = it->second;
        for(size_t i = 0; i < l.size(); i++)
        {
            debug() << "    Pair:\n";
            debug() << "      " << l[i].first->to_string() << "\n";
            debug() << "      " << l[i].second->to_string() << "\n";
        }
    }
    */
    
    goto Lcont;

    Lpromote_all:
    /* promote all physical register */
    {
        for(reg_creator_list::iterator it = reg_creators.begin(); it != reg_creators.end(); ++it)
        {
            std::set< const schedule_unit * >::iterator it2;
            for(it2 = it->second.begin(); it2 != it->second.end(); ++it2)
                promote_phys_register(dag, *it2, it->first, modified);
        }
    }

    Lcont:
    status.set_modified_graph(modified);
    status.set_junction(false);
    status.set_deadlock(false);

    XTM_FW_STOP(handle_physical_regs)
    /* forward */
    s.schedule(dag, c);

    status.end_transformation();
    debug() << "<--- handle_physical_regs::transform\n";
}

void handle_physical_regs::promote_phys_register(
    schedule_dag& dag,
    const schedule_unit *unit,
    schedule_dep::reg_t reg,
    bool& modified) const
{
    if(!m_promote)
        return;
    std::vector< schedule_dep > to_remove;
    std::vector< schedule_dep > to_add;
    schedule_dep::reg_t id = dag.generate_unique_reg_id();
    /* loop through all successors S */
    for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
    {
        schedule_dep dep = dag.get_succs(unit)[i];
        /* only consider phys reg deps on R */
        if(!dep.is_phys() || dep.reg() != reg)
            continue;
        /* change the kind from phys to virt */
        dep.set_kind(pasched::schedule_dep::virt_dep);
        dep.set_reg(id);
        dag.modify_dep(dag.get_succs(unit)[i], dep);
    }

    modified = true;
}

}
