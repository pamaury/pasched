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

class basic_list_scheduler : public pasched::scheduler
{
    public:
    basic_list_scheduler();
    virtual ~basic_list_scheduler();

    virtual void schedule(pasched::schedule_dag& d, pasched::schedule_chain& c) const;
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
