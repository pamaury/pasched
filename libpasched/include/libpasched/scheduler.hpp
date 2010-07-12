#ifndef __PAMAURY_SCHEDULER_HPP__
#define __PAMAURY_SCHEDULER_HPP__

#include "config.hpp"
#include "sched-dag.hpp"
#include "sched-chain.hpp"

namespace PAMAURY_SCHEDULER_NS
{

class scheduler
{
    public:
    scheduler();
    virtual ~scheduler();

    virtual void schedule(schedule_dag& dag, schedule_chain& sc) const = 0;

    protected:
};

/**
 * Mimimum Register Instruction Scheduling
 * optimal solution using an alternative ilp
 * formulation
 */
class mris_ilp_scheduler : public scheduler
{
    public:
    mris_ilp_scheduler();
    virtual ~mris_ilp_scheduler();

    virtual void schedule(schedule_dag& dag, schedule_chain& sc) const;

    protected:
};

}

#endif // __PAMAURY_SCHEDULER_HPP__
