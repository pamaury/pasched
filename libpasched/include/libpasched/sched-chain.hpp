#ifndef __PAMAURY_SCHED_CHAIN_HPP__
#define __PAMAURY_SCHED_CHAIN_HPP__

#include "config.hpp"
#include "sched-unit.hpp"
#include <vector>

namespace PAMAURY_SCHEDULER_NS
{

/**
 * Represent an actual schedule, that is a list of instruction
 * with a given order
 */
class schedule_chain
{
    public:
    schedule_chain();
    virtual ~schedule_chain();

    virtual void emit_unit(const schedule_unit *unit) = 0;

    protected:
};

/**
 * Simple implemented of the interface using a vector
 */
class generic_schedule_chain : public schedule_chain
{
    public:
    generic_schedule_chain(){}
    virtual ~generic_schedule_chain(){}

    virtual void emit_unit(const schedule_unit *unit);

    virtual const std::vector<const schedule_unit *>& get_units() const;

    protected:
    std::vector<const schedule_unit *> m_units;
};

}

#endif // __PAMAURY_SCHED_CHAIN_HPP__
