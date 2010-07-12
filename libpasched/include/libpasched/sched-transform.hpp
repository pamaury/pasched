#ifndef __PAMAURY_SCHED_XFORM_HPP__
#define __PAMAURY_SCHED_XFORM_HPP__

#include "sched-dag.hpp"
#include "sched-chain.hpp"
#include "scheduler.hpp"

namespace PAMAURY_SCHEDULER_NS
{

class transformation
{
    public:

    transformation();
    virtual ~transformation();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c) const = 0;
};

/**
 * Make sure every data dependency has a non-zero register ID
 * and that these IDs are unique among the DAG. This pass handles
 * data dep which already are non-zero but reassign them a new number
 */
class unique_reg_ids : public transformation
{
    public:

    unique_reg_ids();
    virtual ~unique_reg_ids();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c) const;
};

}

#endif /* __PAMAURY_SCHED_XFORM_HPP__ */
