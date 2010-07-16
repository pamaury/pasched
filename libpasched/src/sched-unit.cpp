#include "sched-unit.hpp"
#include <sstream>

namespace PAMAURY_SCHEDULER_NS
{

/**
 * schedule_dep
 */

schedule_dep::reg_t schedule_dep::generate_unique_reg_id()
{
    return g_unique_reg_id++;
}

schedule_dep::reg_t schedule_dep::g_unique_reg_id = 1;

/**
 * schedule_unit
 */

schedule_unit::schedule_unit()
{
}

schedule_unit::~schedule_unit()
{
}

/**
 * chain_schedule_unit
 */
chain_schedule_unit::chain_schedule_unit()
{
    set_internal_register_pressure(0);
}

chain_schedule_unit::~chain_schedule_unit()
{
}

std::string chain_schedule_unit::to_string() const
{
    std::ostringstream oss;
    oss << "[Chain IRP=" << internal_register_pressure() << "]\n";
    for(size_t i = 0; i < m_chain.size(); i++)
    {
        oss << m_chain[i]->to_string();
        if((i + 1) != m_chain.size())
            oss << "\n[Then]\n";
    }

    return oss.str();
}

const chain_schedule_unit *chain_schedule_unit::dup() const
{
    return new chain_schedule_unit(*this);
}

const chain_schedule_unit *chain_schedule_unit::deep_dup() const
{
    chain_schedule_unit *c = new chain_schedule_unit;
    for(size_t i = 0; i < m_chain.size(); i++)
        c->get_chain().push_back(m_chain[i]->deep_dup());
    c->set_internal_register_pressure(internal_register_pressure());
    return c;
}

unsigned chain_schedule_unit::internal_register_pressure() const
{
    return m_irp;
}

void chain_schedule_unit::set_internal_register_pressure(unsigned v)
{
    m_irp = v;
}

const std::vector< const schedule_unit * >& chain_schedule_unit::get_chain() const
{
    return m_chain;
}

std::vector< const schedule_unit * >& chain_schedule_unit::get_chain()
{
    return m_chain;
}

}
