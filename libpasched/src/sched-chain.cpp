#include "sched-chain.hpp"
#include "sched-dag.hpp"
#include "sched-dag-viewer.hpp"
#include <stdexcept>
#include <map>
#include <cassert>
#include <iostream>

namespace PAMAURY_SCHEDULER_NS
{

/**
 * schedule_chain
 */
schedule_chain::schedule_chain()
{
}

schedule_chain::~schedule_chain()
{
}

bool schedule_chain::check_against_dag(const schedule_dag& dag) const
{
    #if 1
    #define fail(msg) return false
    #else
    #define fail(msg) std::runtime_error(msg)
    #endif
    /* trivial check */
    if(dag.get_units().size() != get_unit_count())
        fail("schedule_chain::check_against_dag detected a unit count mismatch");
    /* first build a map of positions */
    std::map< const schedule_unit *, size_t > pos;

    for(size_t i = 0; i < get_unit_count(); i++)
        pos[get_unit_at(i)] = i;

    /* then check each dependency is satisfied */
    for(size_t i = 0; i < dag.get_deps().size(); i++)
    {
        const schedule_dep& d = dag.get_deps()[i];
        if(pos.find(d.from()) == pos.end() ||
                pos.find(d.to()) == pos.end())
            fail("schedule_chain::check_against_dag detected unscheduled unit");
        if(pos[d.from()] >= pos[d.to()])
            fail("schedule_chain::check_against_dag detected unsatisfied dependency");
    }

    /* then check that the same physical register is not alive twice at the same time */
    std::map< schedule_dep::reg_t, size_t > phys_reg_uses;
    for(size_t i = 0; i < get_unit_count(); i++)
    {
        const schedule_unit *unit = get_unit_at(i);
        /* destroy physical registers if any */
        for(size_t j = 0; j < dag.get_preds(unit).size(); j++)
        {
            const schedule_dep& dep = dag.get_preds(unit)[j];
            if(!dep.is_phys())
                continue;
            assert(phys_reg_uses.find(dep.reg()) != phys_reg_uses.end() && "inconsistent phys reg uses map");
            /* remove this unit from the use count of the physical register */
            if((--phys_reg_uses[dep.reg()]) == 0)
                /* if the set is now empty, remove entry */
                phys_reg_uses.erase(dep.reg());
        }
        /* create physical registers if any */
        for(size_t j = 0; j < dag.get_succs(unit).size(); j++)
        {
            const schedule_dep& dep = dag.get_succs(unit)[j];
            if(!dep.is_phys())
                continue;
            /* if the physical register is already alive, then the schedule is not valid */
            if(phys_reg_uses.find(dep.reg()) != phys_reg_uses.end())
                fail("schedule_chain::check_against_dag detected that the same physical register is alive at the same point"
                        "but with different creators, this will result in miscompilation!");
        }
        for(size_t j = 0; j < dag.get_succs(unit).size(); j++)
        {
            const schedule_dep& dep = dag.get_succs(unit)[j];
            if(!dep.is_phys())
                continue;
            phys_reg_uses[dep.reg()]++;
        }
    }

    assert(phys_reg_uses.size() == 0 && "not all physical register have been destroyed: partial dag ?");
    
    return true;
}

size_t schedule_chain::compute_rp_against_dag(const schedule_dag& dag, bool ignore_external_reg) const
{
    std::map< schedule_dep::reg_t, size_t > nb_use_left;
    size_t rp = 0;

    /* if the chain is only a subgraph of the DAG, handle it */
    if(get_unit_count() < dag.get_units().size() && ignore_external_reg)
    {
        schedule_dag *cpy = dag.dup();
        for(size_t u = 0; u < dag.get_units().size(); u++)
        {
            bool ok = false;
            for(size_t i = 0; i < get_unit_count(); i++)
                ok = ok || get_unit_at(i) == dag.get_units()[u];
            if(!ok)
                cpy->remove_unit(dag.get_units()[u]);
        }
        
        size_t rp = compute_rp_against_dag(*cpy);
        delete cpy;
        return rp;
    }

    for(size_t i = 0; i < get_unit_count(); i++)
    {
        const schedule_unit *unit = get_unit_at(i);
        /* use & destroy regs */
        std::set< schedule_dep::reg_t > set;
        std::set< schedule_dep::reg_t >::iterator it;

        set = dag.get_reg_use(unit);
        for(it = set.begin(); it != set.end(); ++it)
        {
            assert(nb_use_left.find(*it) != nb_use_left.end() && "Used variable is not alive !");
            assert(nb_use_left[*it] > 0 && "Variable is use more times than expected !");

            nb_use_left[*it]--;
            if(nb_use_left[*it] == 0)
                nb_use_left.erase(*it);
        }
        /* update RP */
        rp = std::max(rp, nb_use_left.size() + unit->internal_register_pressure());
        /* create regs */
        set = dag.get_reg_create(unit);
        for(it = set.begin(); it != set.end(); ++it)
        {
            assert(nb_use_left.find(*it) == nb_use_left.end() &&
                "Created variable is already alive ! Either this is wrong or fucked up with physical registers, "
                "check with schedule_chain::check_against_dag");

            nb_use_left[*it] = 0;
            for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
            {
                const schedule_dep& dep = dag.get_succs(unit)[i];
                if(dep.is_data() && dep.reg() == *it)
                    nb_use_left[*it]++;
            }
        }
        /* update RP */
        rp = std::max(rp, nb_use_left.size());
    }

    if(ignore_external_reg)
    {
        assert(nb_use_left.size() == 0 && "Variables still alive at end of schedule !");
    }
    else
    {
        rp = std::max(rp, nb_use_left.size());
    }

    return rp;
}

size_t schedule_chain::find_unit(const schedule_unit *unit) const
{
    size_t i = 0;
    while(i < get_unit_count())
    {
        if(get_unit_at(i) == unit)
            break;
        i++;
    }
    return i;
}

/**
 * generic_schedule_chain
 */
generic_schedule_chain::generic_schedule_chain()
{
}

generic_schedule_chain::~generic_schedule_chain()
{
}

size_t generic_schedule_chain::get_unit_count() const
{
    return m_units.size();
}

const schedule_unit *generic_schedule_chain::get_unit_at(size_t pos) const
{
    if(pos >= m_units.size())
        throw std::runtime_error("generic_schedule_chain::get_unit_at: index out of bounds");
    return m_units[pos];
}

void generic_schedule_chain::set_unit_at(size_t pos, const schedule_unit *u)
{
    if(pos >= m_units.size())
        throw std::runtime_error("generic_schedule_chain::set_unit_at: index out of bounds");
    m_units[pos] = u;
}

void generic_schedule_chain::insert_unit_at(size_t pos, const schedule_unit *u)
{
    if(pos > m_units.size())
        throw std::runtime_error("generic_schedule_chain::insert_unit_at: index out of bounds");
    m_units.insert(m_units.begin() + pos, u);
}

void generic_schedule_chain::remove_unit_at(size_t pos)
{
    if(pos >= m_units.size())
        throw std::runtime_error("generic_schedule_chain::remove_unit_at: index out of bounds");
    m_units.erase(m_units.begin() + pos);
}

void generic_schedule_chain::insert_units_at(size_t pos, const std::vector< const schedule_unit * >& v)
{
    if(pos > m_units.size())
        throw std::runtime_error("generic_schedule_chain::insert_units_at: index out of bounds");
    m_units.insert(m_units.begin() + pos, v.begin(), v.end());
}

void generic_schedule_chain::insert_units_at(size_t pos, const schedule_chain& c)
{
    std::vector< const schedule_unit * > v;
    v.reserve(c.get_unit_count());
    for(size_t i = 0; i < c.get_unit_count(); i++)
        v[i] = c.get_unit_at(i);
    insert_units_at(pos, v);
}

void generic_schedule_chain::expand_unit_at(size_t pos, const std::vector< const schedule_unit * >& v)
{
    if(pos >= m_units.size())
        throw std::runtime_error("generic_schedule_chain::expand_unit_at: index out of bounds");

    if(v.size() == 0)
    {
        remove_unit_at(pos);
    }
    else
    {
        m_units.insert(m_units.begin() + (1 + pos), v.begin() + 1, v.end());
        m_units[pos] = v[0];
    }
}

void generic_schedule_chain::expand_unit_at(size_t pos, const schedule_chain& c)
{
    std::vector< const schedule_unit * > v;
    v.resize(c.get_unit_count());
    for(size_t i = 0; i < c.get_unit_count(); i++)
        v[i] = c.get_unit_at(i);
    expand_unit_at(pos, v);
}

void generic_schedule_chain::append_unit(const schedule_unit *unit)
{
    m_units.push_back(unit);
}

const std::vector<const schedule_unit *>& generic_schedule_chain::get_units() const
{
    return m_units;
}

void generic_schedule_chain::clear()
{
    m_units.clear();
}

}
