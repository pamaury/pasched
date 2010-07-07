#include <sched-transform.hpp>
#include <sstream>
#include <stdexcept>

namespace PAMAURY_SCHEDULER_NS
{

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


/**
 * schedule_chain_transformation
 */
schedule_chain_transformation::schedule_chain_transformation()
{
}

schedule_chain_transformation::~schedule_chain_transformation()
{
}

/**
 * dummy_schedule_chain_transformation
 */
dummy_schedule_chain_transformation::dummy_schedule_chain_transformation()
{
}

dummy_schedule_chain_transformation::~dummy_schedule_chain_transformation()
{
}

void dummy_schedule_chain_transformation::transform(schedule_chain&) const
{
}

/**
 * chain_expander_schedule_chain_transformation
 */
chain_expander_schedule_chain_transformation::chain_expander_schedule_chain_transformation()
{
}

chain_expander_schedule_chain_transformation::~chain_expander_schedule_chain_transformation()
{
}

void chain_expander_schedule_chain_transformation::transform(schedule_chain&) const
{
    throw std::runtime_error("chain_expander_schedule_chain_transformation::transform is unimplemented");
}

const std::vector< const chain_schedule_unit * >& chain_expander_schedule_chain_transformation::expand_list() const
{
    return m_expand_list;
}

std::vector< const chain_schedule_unit * >& chain_expander_schedule_chain_transformation::expand_list()
{
    return m_expand_list;
}

/**
 * schedule_dag_tranformation
 */
schedule_dag_tranformation::schedule_dag_tranformation()
{
}

schedule_dag_tranformation::~schedule_dag_tranformation()
{
}


}

