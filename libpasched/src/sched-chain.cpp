#include "sched-chain.hpp"
#include <stdexcept>

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
        return;
    }

    m_units.insert(m_units.begin() + (1 + pos), v.begin() + 1, v.end());
    m_units[pos] = v[0];
}

void generic_schedule_chain::expand_unit_at(size_t pos, const schedule_chain& c)
{
    std::vector< const schedule_unit * > v;
    v.reserve(c.get_unit_count());
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

}
