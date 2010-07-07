#ifndef __PAMAURY_SCHED_UNIT_HPP__
#define __PAMAURY_SCHED_UNIT_HPP__

#include "config.hpp"
#include <string>
#include <vector>

namespace PAMAURY_SCHEDULER_NS
{

class schedule_unit;

class schedule_dep
{
    public:
    enum dep_kind
    {
        data_dep, // data dependency: register
        order_dep // other: memory, artificial, ...
    };

    typedef unsigned reg_t;

    inline schedule_dep()
        :m_from(0), m_to(0), m_kind(order_dep), m_reg(0) {}

    inline schedule_dep(const schedule_unit *from, const schedule_unit *to, dep_kind kind)
        :m_from(from), m_to(to), m_kind(kind), m_reg(0) {}

    inline schedule_dep(const schedule_unit *from, const schedule_unit *to, dep_kind kind, reg_t reg)
        :m_from(from), m_to(to), m_kind(kind), m_reg(reg) {}

    inline dep_kind kind() const { return m_kind; }
    inline void set_kind(dep_kind k) { m_kind = k; }

    inline reg_t reg() const { return m_reg; }
    inline void set_reg(reg_t r) { m_reg = r; }

    inline const schedule_unit *from() const { return m_from; }
    inline void set_from(const schedule_unit *u) { m_from = u; }

    inline const schedule_unit *to() const { return m_to; }
    inline void set_to(const schedule_unit *u) { m_to = u; }

    bool operator==(const schedule_dep& d) const
    {
        return m_from == d.m_from &&
                m_to == d.m_to &&
                m_kind == d.m_kind &&
                m_reg == d.m_reg;
    }

    private:
    const schedule_unit *m_from;
    const schedule_unit *m_to;
    dep_kind m_kind;
    reg_t m_reg; // the register for dependencies, 0 otherwise (memory, artificial, ...)
};

class schedule_unit
{
    public:
    schedule_unit();
    virtual ~schedule_unit();

    virtual std::string to_string() const = 0;

    virtual const schedule_unit *dup() const = 0;

    /**
     * Internal egister pressure when executing this schedule unit
     * For example, if you combine two instructions like
     *   a <- ...
     *   res <- ...a...
     * Into one schedule unit, then the internal register pressure
     * is one because of the hidden 'a'.
     */
    virtual unsigned internal_register_pressure() const = 0;
};

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

}

#endif // __PAMAURY_SCHED_UNIT_HPP__
