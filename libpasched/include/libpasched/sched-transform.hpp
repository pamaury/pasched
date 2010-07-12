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

class glued_transformation_scheduler : public scheduler
{
    public:
    glued_transformation_scheduler(const transformation *tranform, const scheduler *sched);
    virtual ~glued_transformation_scheduler();

    virtual void schedule(schedule_dag& d, schedule_chain& c) const;

    protected:
    const transformation *m_transform;
    const scheduler *m_scheduler;
};

class packed_transformation : public transformation
{
    public:
    packed_transformation(const transformation *first, const transformation *second);
    ~packed_transformation();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c) const;

    protected:
    const transformation *m_first;
    const transformation *m_second;
};

class transformation_pipeline : public transformation
{
    public:
    transformation_pipeline();
    ~transformation_pipeline();

    virtual void add_stage(const transformation *tranform);

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c) const;
    
    protected:
    std::vector< const transformation * > m_pipeline;
    std::vector< packed_transformation * > m_packers;
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

/**
 * Delete order dependencies that are redundant because they are already
 * enforced by a combinaison of data&order dependencies.
 * They do not change the scheduling but simplify the graph.
 */
class strip_useless_order_deps : public transformation
{
    public:
    strip_useless_order_deps();
    virtual ~strip_useless_order_deps();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c) const;
};

/**
 *
 */
class smart_fuse_two_units : public transformation
{
    public:
    smart_fuse_two_units(bool allow_non_optimal_irp_calculation);
    virtual ~smart_fuse_two_units();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c) const;

    public:
    bool m_allow_non_optimal_irp_calculation;
};

}

#endif /* __PAMAURY_SCHED_XFORM_HPP__ */
