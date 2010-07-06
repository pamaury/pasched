#ifndef __PAMAURY_SCHED_DAG_HPP__
#define __PAMAURY_SCHED_DAG_HPP__

#include <vector>
#include <map>
#include "config.hpp"
#include "sched-unit.hpp"

namespace PAMAURY_SCHEDULER_NS
{

class schedule_dag
{
    public:
    inline schedule_dag(){}
    inline virtual ~schedule_dag(){}

    virtual const std::vector< const schedule_unit * >& get_roots() const = 0;
    virtual const std::vector< const schedule_unit * >& get_leaves() const = 0;
    virtual const std::vector< const schedule_unit * >& get_units() const = 0;

    virtual const std::vector< schedule_dep >& get_succs(const schedule_unit *su) const = 0;
    virtual const std::vector< schedule_dep >& get_preds(const schedule_unit *su) const = 0;
    virtual const std::vector< schedule_dep >& get_deps() const = 0;

    // any graph change might invalidate all pointers above !
    virtual void add_dependency(schedule_dep d) = 0;
    virtual void remove_dependency(schedule_dep d) = 0;
    virtual void add_unit(const schedule_unit *unit) = 0;
    virtual void remove_unit(const schedule_unit *unit) = 0;
    virtual void clear() = 0;

    // trivial implementation that can be optimized
    virtual void add_dependencies(const std::vector< schedule_dep >& deps);
    virtual void remove_dependencies(const std::vector< schedule_dep >& deps);
    virtual void add_units(const std::vector< const schedule_unit * >& units);
    virtual void remove_units(const std::vector< const schedule_unit * >& units);
};

// generic implementation, independent of schedule_unit
class generic_schedule_dag : public schedule_dag
{
    public:
    inline generic_schedule_dag(){}
    inline virtual ~generic_schedule_dag(){}

    virtual const std::vector< const schedule_unit *>& get_roots() const { return m_roots; }
    virtual const std::vector< const schedule_unit *>& get_leaves() const { return m_leaves; }
    virtual const std::vector< const schedule_unit *>& get_units() const { return m_units; }

    virtual const std::vector< schedule_dep >& get_succs(const schedule_unit *su) const { return m_unit_map[su].succs; }
    virtual const std::vector< schedule_dep >& get_preds(const schedule_unit *su) const { return m_unit_map[su].preds; }
    virtual const std::vector< schedule_dep >& get_deps() const { return m_deps; }

    virtual void add_dependency(schedule_dep d);
    virtual void remove_dependency(schedule_dep d);
    virtual void add_unit(const schedule_unit *unit);
    virtual void remove_unit(const schedule_unit *unit);

    virtual bool is_consistent(std::string *out_msg = 0) const;

    virtual void clear();

    protected:
    struct su_data
    {
        std::vector< schedule_dep > preds, succs;
    };
    
    std::vector< const schedule_unit * > m_units;
    std::vector< const schedule_unit * > m_roots;
    std::vector< const schedule_unit * > m_leaves;
    std::vector< schedule_dep > m_deps;
    mutable std::map< const schedule_unit *, su_data >  m_unit_map;
};

struct dag_printer_opt
{
    enum dag_printer_opt_type
    {
        /* Color a node */
        po_color_node,
    };

    dag_printer_opt_type type;
    struct
    {
        /* Which node */
        const schedule_unit *unit;
        /* Which color */
        std::string color;
    }color_node;
};

void dump_schedule_dag_to_dot_file(const schedule_dag& dag,
    const char *filename,
    const std::vector< dag_printer_opt >& opts = std::vector< dag_printer_opt >());

}

#endif // __PAMAURY_SCHED_DAG_HPP__
