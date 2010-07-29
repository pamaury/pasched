#ifndef __PAMAURY_SCHED_CHAIN_HPP__
#define __PAMAURY_SCHED_CHAIN_HPP__

#include "config.hpp"
#include "sched-unit.hpp"
#include <vector>

namespace PAMAURY_SCHEDULER_NS
{

/**
 * Represent an actual schedule, that is a list of instruction
 * with a given order
 */
class schedule_dag;

class schedule_chain
{
    public:
    schedule_chain();
    virtual ~schedule_chain();

    virtual size_t get_unit_count() const = 0;
    virtual const schedule_unit * get_unit_at(size_t pos) const = 0;
    virtual void set_unit_at(size_t pos, const schedule_unit *) = 0;
    virtual void insert_unit_at(size_t pos, const schedule_unit *) = 0;
    virtual void remove_unit_at(size_t pos) = 0;
    virtual void insert_units_at(size_t pos, const std::vector< const schedule_unit * >&) = 0;
    virtual void insert_units_at(size_t pos, const schedule_chain& c) = 0;
    virtual void expand_unit_at(size_t pos, const std::vector< const schedule_unit * >&) = 0;
    virtual void expand_unit_at(size_t pos, const schedule_chain& c) = 0;
    
    virtual void append_unit(const schedule_unit *unit) = 0;

    virtual bool check_against_dag(const schedule_dag& dag) const;
    /**
     * Compute the register pressure of the chain with respect to a DAG.
     * If ignore_external_reg is set to true, it is like if the DAG was restricted to the nodes of the chain.
     * If it is false, then the register created in the chain but not killed in it are taken into account */
    virtual size_t compute_rp_against_dag(const schedule_dag& dag, bool ignore_external_reg = true) const;

    protected:
};

/**
 * Simple implementation of the interface using a vector
 */
class generic_schedule_chain : public schedule_chain
{
    public:
    generic_schedule_chain();
    virtual ~generic_schedule_chain();

    virtual size_t get_unit_count() const;
    virtual const schedule_unit *get_unit_at(size_t pos) const;
    virtual void set_unit_at(size_t pos, const schedule_unit *);
    virtual void insert_unit_at(size_t pos, const schedule_unit *);
    virtual void remove_unit_at(size_t pos);
    virtual void insert_units_at(size_t pos, const std::vector< const schedule_unit * >& v);
    virtual void insert_units_at(size_t pos, const schedule_chain& c);
    virtual void expand_unit_at(size_t pos, const std::vector< const schedule_unit * >& v);
    virtual void expand_unit_at(size_t pos, const schedule_chain& c);
    
    virtual void append_unit(const schedule_unit *unit);
    
    virtual const std::vector<const schedule_unit *>& get_units() const;

    protected:
    std::vector<const schedule_unit *> m_units;
};

}

#endif // __PAMAURY_SCHED_CHAIN_HPP__
