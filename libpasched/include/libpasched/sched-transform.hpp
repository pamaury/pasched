#ifndef __PAMAURY_SCHED_XFORM_HPP__
#define __PAMAURY_SCHED_XFORM_HPP__

#include "sched-dag.hpp"
#include "sched-chain.hpp"
#include "scheduler.hpp"

namespace PAMAURY_SCHEDULER_NS
{

class transformation_status
{
    public:
    transformation_status();
    virtual ~transformation_status();

    /** Called at the beginning of a transformation */
    virtual void begin_transformation() = 0;
    /** Called at the end, when everything is schedule and reassembled */
    virtual void end_transformation() = 0;

    /** did the transformation modified the input graph ? */
    virtual void set_modified_graph(bool m) = 0;
    virtual bool has_modified_graph() const = 0;

    /** did/will the transformation was/be a deadlock and won't/didn't call the
     * scheduler at all ? */
    virtual void set_deadlock(bool d) = 0;
    virtual bool is_deadlock() const = 0;

    /** did/will the transformation was/be a junction and will/did call the
     * scheduler several times ? */
    virtual void set_junction(bool j) = 0;
    virtual bool is_junction() const = 0;
};

class basic_status : public transformation_status
{
    public:
    basic_status();
    virtual ~basic_status();

    virtual void begin_transformation();
    virtual void end_transformation();

    /** did the transformation modified the input graph ? */
    virtual void set_modified_graph(bool m);
    virtual bool has_modified_graph() const;

    /** did/will the transformation was/be a deadlock and won't/didn't call the
     * scheduler at all ? */
    virtual void set_deadlock(bool d);
    virtual bool is_deadlock() const;

    /** did/will the transformation was/be a junction and will/did call the
     * scheduler several times ? */
    virtual void set_junction(bool j);
    virtual bool is_junction() const;

    protected:
    /** Set to true if the transform acted as a pass-thru.
     * This field must be set before the first call to the scheduler */
    bool m_mod;
    bool m_deadlock;
    bool m_junction;
};

class transformation
{
    public:
    transformation();
    virtual ~transformation();

    /**
     * Perform a transform on the graph D given as input and then call
     * the scheduler S on any number of graphs need to schedule D. The resulting
     * schedule has to be *appended* to C.
     */
    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const = 0;
};

class glued_transformation_scheduler : public scheduler
{
    public:
    glued_transformation_scheduler(const transformation *tranform, const scheduler *sched,
        transformation_status& status);
    virtual ~glued_transformation_scheduler();

    virtual void schedule(schedule_dag& d, schedule_chain& c) const;

    protected:
    const transformation *m_transform;
    const scheduler *m_scheduler;
    mutable bool m_tranform_res;
    transformation_status& m_status;
};

class packed_status : public transformation_status
{
    public:
    packed_status(transformation_status& status);
    virtual ~packed_status();

    virtual void begin_transformation();
    virtual void end_transformation();

    virtual void set_modified_graph(bool m);
    virtual bool has_modified_graph() const;
    virtual void set_deadlock(bool d);
    virtual bool is_deadlock() const;
    virtual void set_junction(bool j);
    virtual bool is_junction() const;

    protected:
    int m_level;
    transformation_status& m_status;
};

class packed_transformation : public transformation
{
    public:
    packed_transformation(const transformation *first, const transformation *second);
    ~packed_transformation();

    virtual void set_transformation(bool first, const transformation *t);

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;

    protected:
    const transformation *m_first;
    const transformation *m_second;
};

class transformation_pipeline : public transformation
{
    public:
    transformation_pipeline();
    ~transformation_pipeline();

    virtual void add_stage(const transformation *transform);

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;
    
    protected:
    std::vector< const transformation * > m_pipeline;
    std::vector< packed_transformation * > m_packers;
};

class auxillary_transformation_loop : public transformation
{
    public:
    auxillary_transformation_loop(const transformation *x);
    ~auxillary_transformation_loop();

    virtual void set_transformation(const transformation *t);

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;
    
    protected:
    const transformation *m_transform;
};

class transformation_loop : public transformation
{
    public:
    transformation_loop(const transformation *x);
    ~transformation_loop();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;
    
    protected:
    const transformation *m_transform;
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

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;
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

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;
};

/**
 *
 */
class smart_fuse_two_units : public transformation
{
    public:
    smart_fuse_two_units(bool allow_non_optimal_irp_calculation);
    virtual ~smart_fuse_two_units();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;

    protected:
    bool m_allow_non_optimal_irp_calculation;
};

/**
 * If the graph can be splitted into two subgraph G and H such that
 * the only depdencies between them are:
 * 1) order dep
 * 2) go from G to H (G->H)
 * Then it means that we can schedule G as a whole and then H as a whole
 * because they do not interfere. So we can cut all these dependencies
 * because they make the graph appear as more complicated than it really
 * is.
 */
class simplify_order_cuts : public transformation
{
    public:
    simplify_order_cuts();
    virtual ~simplify_order_cuts();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;

    protected:
    void do_transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status, int level) const;
};

/**
 * If there is a node U such that all children for the same dependency
 * are U(1) ... U(k) satisfy the property that all U(i) are reachable
 * from, say, U(1), then we can cut all (U,U(i)) dep for i>=1
 * and add (U(1),U(i)) instead
 */
class split_def_use_dom_use_deps : public transformation
{
    public:
    split_def_use_dom_use_deps(bool generate_unique_reg_id = true);
    virtual ~split_def_use_dom_use_deps();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;

    protected:
    bool m_generate_new_reg_ids;
};

/**
 *
 */
class break_symmetrical_branch_merge : public transformation
{
    public:
    break_symmetrical_branch_merge();
    virtual ~break_symmetrical_branch_merge();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;
};

/**
 *
 */
class collapse_chains : public transformation
{
    public:
    collapse_chains();
    virtual ~collapse_chains();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;
};

/**
 *
 */
class split_merge_branch_units : public transformation
{
    public:
    split_merge_branch_units();
    virtual ~split_merge_branch_units();

    virtual void transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status) const;

    protected:
    virtual void do_transform(schedule_dag& d, const scheduler& s, schedule_chain& c,
        transformation_status& status, int level) const;
};

}

#endif /* __PAMAURY_SCHED_XFORM_HPP__ */
