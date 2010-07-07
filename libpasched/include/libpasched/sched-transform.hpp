#ifndef __PAMAURY_SCHED_XFORM_HPP__
#define __PAMAURY_SCHED_XFORM_HPP__

#include "sched-dag.hpp"
#include "sched-chain.hpp"

namespace PAMAURY_SCHEDULER_NS
{



class schedule_chain_transformation
{
    public:
    schedule_chain_transformation();
    virtual ~schedule_chain_transformation();

    virtual void transform(schedule_chain& sc) const = 0;
};

class dummy_schedule_chain_transformation : public schedule_chain_transformation
{
    public:
    dummy_schedule_chain_transformation();
    virtual ~dummy_schedule_chain_transformation();

    virtual void transform(schedule_chain& sc) const;
};

class schedule_dag_tranformation
{
    public:

    schedule_dag_tranformation();
    virtual ~schedule_dag_tranformation();

    virtual schedule_chain_transformation *transform(schedule_dag& sc) const = 0;
};

}

#endif /* __PAMAURY_SCHED_XFORM_HPP__ */
