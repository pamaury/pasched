#ifndef __PAMAURY_SCHED_DAG_HPP__
#define __PAMAURY_SCHED_DAG_HPP__

#include <vector>
#include <map>
#include <set>
#include "config.hpp"
#include "sched-unit.hpp"

namespace PAMAURY_SCHEDULER_NS
{

/**
 * Represent a DAG to schedule with all the data and order dependencies
 */
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

    /** State */
    /* keep track of modification state */
    virtual bool modified() const = 0;
    virtual void set_modified(bool mod) = 0;

    /** Single unit/dep add/removal */
    // any graph change might invalidate all pointers above !
    virtual void add_dependency(schedule_dep d) = 0;
    virtual void remove_dependency(schedule_dep d) = 0;
    virtual void add_unit(const schedule_unit *unit) = 0;
    virtual void remove_unit(const schedule_unit *unit) = 0;
    virtual void clear() = 0;

    /** Massive unit/dev add/removal */
    virtual void add_dependencies(const std::vector< schedule_dep >& deps);
    virtual void remove_dependencies(const std::vector< schedule_dep >& deps);
    virtual void add_units(const std::vector< const schedule_unit * >& units);
    virtual void remove_units(const std::vector< const schedule_unit * >& units);

    /** Helper functions for graph exploration */
    enum
    {
        rf_follow_preds_order = 1 << 0, /* follow order pred deps */
        rf_follow_preds_data = 1 << 1, /* follow data pred deps */
        rf_follow_preds = rf_follow_preds_order | rf_follow_preds_data,
        rf_follow_succs_order = 1 << 2, /* follow order succ deps */
        rf_follow_succs_data = 1 << 3, /* follow daat succ deps */
        rf_follow_succs = rf_follow_succs_order | rf_follow_succs_data,
        rf_include_unit = 1 << 4, /* include initial unit */
        rf_immediate = 1 << 5 /* restrict to immediate neighbourhood */
    };

    virtual std::set< const schedule_unit * > get_reachable(
        const schedule_unit *unit, unsigned flags);

    /** Helper functions for register information */
    virtual std::set< schedule_dep::reg_t > get_reg_create(
        const schedule_unit *unit);
    virtual std::set< schedule_dep::reg_t > get_reg_use(
        const schedule_unit *unit);
    virtual std::set< schedule_dep::reg_t > get_reg_destroy(
        const schedule_unit *unit);
};

/**
 * Generic implementation of the interface
 */
class generic_schedule_dag : public schedule_dag
{
    public:
    generic_schedule_dag();
    virtual ~generic_schedule_dag();

    virtual const std::vector< const schedule_unit *>& get_roots() const { return m_roots; }
    virtual const std::vector< const schedule_unit *>& get_leaves() const { return m_leaves; }
    virtual const std::vector< const schedule_unit *>& get_units() const { return m_units; }

    virtual const std::vector< schedule_dep >& get_succs(const schedule_unit *su) const { return m_unit_map[su].succs; }
    virtual const std::vector< schedule_dep >& get_preds(const schedule_unit *su) const { return m_unit_map[su].preds; }
    virtual const std::vector< schedule_dep >& get_deps() const { return m_deps; }

    virtual bool modified() const;
    virtual void set_modified(bool mod);

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
    bool m_modified;
};

/**
 * Debug functions to print a DAG to a DOT file which can later be rendered
 * by Graphivz
 */
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
