#ifndef __PAMAURY_SCHED_CHAIN_HPP__
#define __PAMAURY_SCHED_CHAIN_HPP__

#include "config.hpp"
#include "sched-unit.hpp"

namespace PAMAURY_SCHEDULER_NS
{

class schedule_chain
{
    public:
    schedule_chain(){}
    virtual ~schedule_chain(){}

    virtual void emit_unit(const schedule_unit *unit) = 0;
    virtual void emit_nop() = 0;

    protected:
};

// simply represent a schedule chain by a vector and nops by NULL
class generic_schedule_chain : public schedule_chain
{
    public:
    generic_schedule_chain(){}
    virtual ~generic_schedule_chain(){}

    inline virtual void emit_unit(const schedule_unit *unit) { m_units.push_back(unit); }
    inline virtual void emit_nop() { m_units.push_back(0); }

    inline virtual const std::vector<const schedule_unit *>& get_units() const { return m_units; }

    protected:
    std::vector<const schedule_unit *> m_units;
};

}

#endif // __PAMAURY_SCHED_CHAIN_HPP__
