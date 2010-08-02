#ifndef __PAMAURY_SCHEDULER_HPP__
#define __PAMAURY_SCHEDULER_HPP__

#include "config.hpp"
#include "sched-dag.hpp"
#include "sched-chain.hpp"
#include "time-tools.hpp"

namespace PAMAURY_SCHEDULER_NS
{

#define STM_DECLARE(name) STM_STAT(namespace { time_stat stm_##name("stm-" #name); })
#define STM_START(name) STM_STAT(stm_##name.get_timer().start();)
#define STM_STOP(name) STM_STAT(stm_##name.get_timer().stop();)

class scheduler
{
    public:
    scheduler();
    virtual ~scheduler();

    virtual void schedule(schedule_dag& dag, schedule_chain& sc) const = 0;

    protected:
};

/**
 * Random scheduler
 * (random apply to quality, the scheduler is perfectly deterministic :))
 */
class rand_scheduler : public pasched::scheduler
{
    public:
    rand_scheduler();
    virtual ~rand_scheduler();

    virtual void schedule(pasched::schedule_dag& d, pasched::schedule_chain& c) const;
};

/**
 * Simple list scheduler that tries to do reduce the register pressure
 */
class simple_rp_scheduler : public pasched::scheduler
{
    public:
    simple_rp_scheduler();
    virtual ~simple_rp_scheduler();

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
    /* Timeout in ms, 0 for no timeout */
    mris_ilp_scheduler(const scheduler *fallback_sched = 0, size_t fallback_timeout = 0, bool verbose = false);
    virtual ~mris_ilp_scheduler();

    virtual void schedule(schedule_dag& dag, schedule_chain& sc) const;

    protected:
    const scheduler *m_fallback_sched;
    size_t m_timeout;
    bool m_verbose;
};

/**
 * 
 */
class exp_scheduler : public scheduler
{
    public:
    /* Timeout in ms, 0 for no timeout */
    exp_scheduler(const scheduler *fallback_sched = 0, size_t fallback_timeout = 0, bool verbose = false);
    virtual ~exp_scheduler();

    virtual void schedule(schedule_dag& dag, schedule_chain& sc) const;

    protected:
    const scheduler *m_fallback_sched;
    size_t m_timeout;
    bool m_verbose;
};

}

#endif // __PAMAURY_SCHEDULER_HPP__
