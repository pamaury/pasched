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
    
    void generic_schedule_chain::append_unit(const schedule_unit *unit)
    {
        m_units.push_back(unit);
    }

    const std::vector<const schedule_unit *>& generic_schedule_chain::get_units() const
    {
        return m_units;
    }
}
