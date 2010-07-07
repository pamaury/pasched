#ifndef __PAMAURY_SCHED_XFORM_HPP__
#define __PAMAURY_SCHED_XFORM_HPP__

#include "sched-dag.hpp"
#include "sched-chain.hpp"

namespace PAMAURY_SCHEDULER_NS
{

class chain_schedule_unit : public schedule_unit
{
    public:
    chain_schedule_unit();
    virtual ~chain_schedule_unit();

    virtual std::string to_string() const;

    virtual const chain_schedule_unit *dup() const;

    virtual unsigned internal_register_pressure() const;

    virtual void set_internal_register_pressure(unsigned v);

    const std::vector< const schedule_unit * >& get_chain() const;
    std::vector< const schedule_unit * >& get_chain();

    protected:
    unsigned m_irp;
    std::vector< const schedule_unit * > m_chain;
};

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

class chain_expander : public schedule_chain_transformation
{
    public:
    chain_expander();
    virtual ~chain_expander();

    virtual void transform(schedule_chain& sc) const;

    virtual const std::vector< const chain_schedule_unit *>& expand_list() const;
    virtual std::vector< const chain_schedule_unit *>& expand_list();

    protected:
    std::vector< const chain_schedule_unit *> m_expand_list;
};

class schedule_dag_tranformation
{
    public:

    schedule_dag_tranformation();
    virtual ~schedule_dag_tranformation();

    virtual const schedule_chain_transformation *transform(schedule_dag& sc) const = 0;
};

/**
 * Make sure every data dependency has a non-zero register ID
 * and that these IDs are unique among the DAG. This pass handles
 * data dep which already are non-zero but reassign them a new number
 */
class unique_reg_ids : public schedule_dag_tranformation
{
    public:

    unique_reg_ids();
    virtual ~unique_reg_ids();

    virtual const schedule_chain_transformation *transform(schedule_dag& sc) const;
};

}

#endif /* __PAMAURY_SCHED_XFORM_HPP__ */
