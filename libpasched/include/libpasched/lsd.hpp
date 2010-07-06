#ifndef __PAMAURY_LSD_HPP__
#define __PAMAURY_LSD_HPP__

#include "config.hpp"
#include "sched-dag.hpp"
#include "sched-chain.hpp"

namespace PAMAURY_SCHEDULER_NS
{

/* LLVM Schedule DAG schedule unit */
class lsd_schedule_unit : public schedule_unit
{
    public:
    lsd_schedule_unit(const std::string& id) : m_id(id) {}
    virtual ~lsd_schedule_unit() {}

    virtual std::string to_string() const;

    const std::string& name() const { return m_name; }
    std::string& name() { return m_name; }

    virtual const lsd_schedule_unit *dup() const;

    virtual unsigned internal_register_pressure() const;

    protected:
    std::string m_id;
    std::string m_name;
};

void build_schedule_dag_from_lsd_file(const char *filename, schedule_dag& dag);

}

#endif // __PAMAURY_LSD_HPP__
