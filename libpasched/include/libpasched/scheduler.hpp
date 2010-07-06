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
    scheduler(const schedule_dag& sd);
    virtual ~scheduler();

    virtual inline const schedule_dag& get_schedule_dag() const
    {
        return m_graph;
    }

    virtual void schedule(schedule_chain& sc) = 0;

    protected:
    const schedule_dag& m_graph;
};

/**
 * Mimimum Register Instruction Scheduling
 * optimal solution using ilp formulation
 */
class mris_ilp_scheduler : public scheduler
{
    public:
    mris_ilp_scheduler(const schedule_dag& sd);
    virtual ~mris_ilp_scheduler();

    virtual void schedule(schedule_chain& sc);

    protected:
};

/**
 * Mimimum Register Instruction Scheduling
 * optimal solution using an alternative ilp
 * formulation
 */
class mris_ilp_scheduler_alt : public scheduler
{
    public:
    mris_ilp_scheduler_alt(const schedule_dag& sd);
    virtual ~mris_ilp_scheduler_alt();

    virtual void schedule(schedule_chain& sc);

    protected:
};

}

#endif // __PAMAURY_SCHEDULER_HPP__
