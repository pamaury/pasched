#include "sched-chain.hpp"

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
    void generic_schedule_chain::emit_unit(const schedule_unit *unit)
    {
        m_units.push_back(unit);
    }

    const std::vector<const schedule_unit *>& generic_schedule_chain::get_units() const
    {
        return m_units;
    }
}
