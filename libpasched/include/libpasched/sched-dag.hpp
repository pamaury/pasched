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
    schedule_dag();
    virtual ~schedule_dag();

    /**
     * Duplicate the graph
     */
    virtual schedule_dag *dup() const = 0;
    /**
     * Duplicate the graph and the schedule units
     */
    virtual schedule_dag *deep_dup() const;
    

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

    /** Modify properties of a dependency. You can't change the from/to properties
     * but can change all others (kind, reg, ...)
     * If several dependencies match the old one, only once is modified */
    virtual void modify_dep(const schedule_dep& old, const schedule_dep& cur);

    /** Massive unit/dev add/removal */
    virtual void add_dependencies(const std::vector< schedule_dep >& deps);
    virtual void remove_dependencies(const std::vector< schedule_dep >& deps);
    virtual void add_units(const std::vector< const schedule_unit * >& units);
    virtual void remove_units(const std::vector< const schedule_unit * >& units);

    /** Create a new register ID which is guaranted to be different from
     * all others in the graph */
    virtual schedule_dep::reg_t generate_unique_reg_id() const;

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
        const schedule_unit *unit, unsigned flags) const;

    /** Helper functions for register information */

    /**
     * Compute the set of register created by a schedule unit
     */
    virtual std::set< schedule_dep::reg_t > get_reg_create(
        const schedule_unit *unit, bool pick_virt = true, bool pick_phys = true) const;
    virtual std::set< schedule_dep::reg_t > get_reg_phys_create(
        const schedule_unit *unit) const;
    virtual std::set< schedule_dep::reg_t > get_reg_virt_create(
        const schedule_unit *unit) const;
    /**
     * Compute the set of registers used by a schedule unit
     */
    virtual std::set< schedule_dep::reg_t > get_reg_use(
        const schedule_unit *unit, bool pick_virt = true, bool pick_phys = true) const;
    virtual std::set< schedule_dep::reg_t > get_reg_phys_use(
        const schedule_unit *unit) const;
    virtual std::set< schedule_dep::reg_t > get_reg_virt_use(
        const schedule_unit *unit) const;
    /**
     * Compute the set of registers used by a schedule unit and
     * which are necessarily destroyed by it (ie last use).
     *
     * NOTE: this function uses a simple method, that is registers
     *       which are used by the unit and of which the unit is the
     *       only use. This is an underapproximation
     */
    virtual std::set< schedule_dep::reg_t > get_reg_destroy(
        const schedule_unit *unit) const;
    /**
     * Compute the set of registers used by a schedule unit and
     * which are necessarily destroyed by it (ie last use).
     *
     * NOTE: this function does a complete analysis of other dependencies
     *       and is thus exact but more expensive
     */
    virtual std::set< schedule_dep::reg_t > get_reg_destroy_exact(
        const schedule_unit *unit) const;
    /**
     * Compute the set of registers used by a schedule unit and
     * which are necessarily not destroyed by it (ie not last use).
     *
     * NOTE: this function does a complete analysis of other dependencies
     *       and is thus exact but more expensive
     */
    virtual std::set< schedule_dep::reg_t > get_reg_dont_destroy_exact(
        const schedule_unit *unit) const;

    /**
     * Fuse two units by removing them from the graph and replacing
     * them by a single chain_schedule_unit. If simulate_if_approx is
     * set to true, then the fusing is not done if the IRP computing
     * can be done optimaly and there is an approximation. In this case,
     * the function will return 0. 
     *
     * NOTE: the return chain_schedule_unit is not const to allow
     *       further modification
     * NOTE: the operation is not necessarily checked for consistency,
     *       so you can produce illegal graphs with this !
     */
    virtual chain_schedule_unit *fuse_units(const schedule_unit *a,
        const schedule_unit *b, bool simulate_if_approx = false,
        bool allow_unsafe_phys_dep_hiding = false);

    /**
     * Duplicate a subgraph of the DAG given by a set of nodes.
     */
    virtual schedule_dag *dup_subgraph(const std::set< const schedule_unit * >& units) const;

    /**
     * Collapse a given set of node into one node given as a parameter.
     */
    virtual void collapse_subgraph(const std::set< const schedule_unit * >& units,
        const schedule_unit *new_unit);

    /**
     * Replace a specified node by another.
     */
    virtual void replace_unit(const schedule_unit *old, const schedule_unit *new_unit);

    /**
     * Check all the predecessors dep of a node for data redundancies
     * and delete them. Similary for successors and for the whole graph.
     */
    virtual void remove_redundant_data_dep_preds(const schedule_unit *unit);
    virtual void remove_redundant_data_dep_succs(const schedule_unit *unit);
    virtual void remove_redundant_data_deps();

    /**
     * Build a path map for the entire graph.
     * It builds to objects: first a 2D-table of boolean and second a map.
     * The [index->unit] map is the one returned by get_units() and the
     * [unit->index] map is the one built by this function.
     *
     * NOTE: the input table is resized and the name map is cleared */
    virtual void build_path_map(std::vector< std::vector< bool > >& path_map,
        std::map< const schedule_unit *, size_t >& name_map) const;

    virtual bool is_consistent(std::string *out_msg = 0) const = 0;
};

/**
 * Generic implementation of the interface
 */
class generic_schedule_dag : public schedule_dag
{
    public:
    generic_schedule_dag();
    virtual ~generic_schedule_dag();

    virtual generic_schedule_dag *dup() const;

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
    virtual void modify_dep(const schedule_dep& old, const schedule_dep& cur);

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
    mutable std::map< const schedule_unit *, su_data > m_unit_map;
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
        /* Color a dependency */
        po_color_dep,
        /* Don't print register names */
        po_hide_dep_labels
    };

    dag_printer_opt_type type;
    struct
    {
        /* Which node */
        const schedule_unit *unit;
        /* Which color */
        std::string color;
    }color_node;
    struct
    {
        /* Which dep (will match all of them !) */
        schedule_dep dep;
        /* Which color (empty if unspecified) */
        std::string color;
        /* Which style (empty if unspecified) */
        std::string style;
        /* Match all or only one ? */
        bool match_all;
    }color_dep;
    struct
    {
        /* Hide data deps register name */
        bool hide_virt;
        /* Ditto for phys */
        bool hide_phys;
        /* Ditto for order */
        bool hide_order;
    }hide_dep_labels;
};

void dump_schedule_dag_to_dot_file(const schedule_dag& dag,
    const char *filename,
    const std::vector< dag_printer_opt >& opts = std::vector< dag_printer_opt >());

}

#endif // __PAMAURY_SCHED_DAG_HPP__
