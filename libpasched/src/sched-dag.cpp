#include "sched-dag.hpp"
#include "tools.hpp"
#include <stdexcept>
#include <queue>

//#define AUTO_CHECK_CONSISTENCY

namespace PAMAURY_SCHEDULER_NS
{

/**
 * Interface Implementation
 */
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

std::set< const schedule_unit * > schedule_dag::get_reachable(const schedule_unit *unit,
    unsigned flags)
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
            if(d.kind() == schedule_dep::data_dep && (flags & rf_follow_preds_data))
                q.push(d.from());
            else if(d.kind() == schedule_dep::order_dep && (flags & rf_follow_preds_order))
                q.push(d.from());
        }

        for(size_t i = 0; i < get_succs(u).size(); i++)
        {
            const schedule_dep& d = get_succs(u)[i];
            if(d.kind() == schedule_dep::data_dep && (flags & rf_follow_succs_data))
                q.push(d.to());
            else if(d.kind() == schedule_dep::order_dep && (flags & rf_follow_succs_order))
                q.push(d.to());
        }
    }

    return s;
}

std::set< schedule_dep::reg_t > schedule_dag::get_reg_create(
    const schedule_unit *unit)
{
    std::set< schedule_dep::reg_t > s;
    for(size_t i = 0; i < get_succs(unit).size(); i++)
        if(get_succs(unit)[i].kind() == schedule_dep::data_dep)
            s.insert(get_succs(unit)[i].reg());
    return s;
}

std::set< schedule_dep::reg_t > schedule_dag::get_reg_use(
    const schedule_unit *unit)
{
    std::set< schedule_dep::reg_t > s;
    for(size_t i = 0; i < get_preds(unit).size(); i++)
        if(get_preds(unit)[i].kind() == schedule_dep::data_dep)
            s.insert(get_preds(unit)[i].reg());
    return s;
}

std::set< schedule_dep::reg_t > schedule_dag::get_reg_destroy(
    const schedule_unit *unit)
{
    std::set< schedule_dep::reg_t > s;
    for(size_t i = 0; i < get_preds(unit).size(); i++)
    {
        const schedule_dep& dep = get_preds(unit)[i];
        if(dep.kind() != schedule_dep::data_dep)
            continue;
        for(size_t j = 0; j < get_succs(dep.from()).size(); j++)
        {
            const schedule_dep& sec_dep = get_succs(dep.from())[j];
            if(sec_dep.kind() == schedule_dep::data_dep &&
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
