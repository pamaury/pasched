#include <iostream>
#include <pasched.hpp>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <iomanip>
#include <vector>
#include <queue>
#include <cassert>
#include <set>
#include <list>
#include <sstream>
#include <cmath>
#include <stdint.h>

/**
 * Shortcuts for often used types
 */
typedef pasched::schedule_dag sched_dag_t;
typedef const pasched::schedule_unit *sched_unit_ptr_t;
typedef pasched::schedule_dep sched_dep_t;
typedef std::set< sched_unit_ptr_t > sched_unit_set_t;
typedef std::vector< sched_unit_ptr_t > sched_unit_vec_t;
typedef std::vector< sched_dep_t > sched_dep_vec_t;
typedef sched_dep_t::reg_t sched_reg_t;
typedef std::set< sched_reg_t > sched_reg_set_t;

/**
 * I/O functions
 */
void ddl_read(const char *filename, pasched::schedule_dag& dag)
{
    pasched::ddl_program p;
    pasched::load_ddl_program_from_ddl_file(filename, p);
    pasched::build_schedule_dag_from_ddl_program(p, dag);
}

void lsd_read(const char *filename, pasched::schedule_dag& dag)
{
    pasched::build_schedule_dag_from_lsd_file(filename, dag);
}

void dot_write(pasched::schedule_dag& dag, const char *filename)
{
    pasched::dump_schedule_dag_to_dot_file(dag, filename);
}

void dotsvg_write(pasched::schedule_dag& dag, const char *filename)
{
    char *name = tmpnam(NULL);
    pasched::dump_schedule_dag_to_dot_file(dag, name);
    system((std::string("dot -Tsvg -o ") + filename + " " + name).c_str());
    remove(name);
}

void lsd_write(pasched::schedule_dag& dag, const char *filename)
{
    pasched::dump_schedule_dag_to_lsd_file(dag, filename);
}

void null_write(pasched::schedule_dag& dag, const char *filename)
{
    (void) dag;
    (void) filename;
}

/**
 * Printing/Debug functions
 */
std::ostream& operator<<(std::ostream& os, sched_unit_ptr_t u)
{
    return os << u->to_string();
}

template<typename T>
std::ostream& operator<<(std::ostream& os, const std::set< T >& s)
{
    typename std::set< T >::const_iterator it = s.begin();
    os << "{";

    while(it != s.end())
    {
        os << *it;
        ++it;
        if(it == s.end())
            break;
        os << ", ";
    }
    
    return os << "}";
}

template<typename T>
std::ostream& operator<<(std::ostream& os, const std::vector< T >& v)
{
    os << "[";

    for(size_t i = 0; i < v.size(); i++)
    {
        os << v[i];
        if((i + 1) != v.size())
            os << ", ";
    }
    
    return os << "]";
}

class dag_accumulator : public pasched::transformation
{
    public:
    dag_accumulator(bool do_deep_copy = true):m_deep(do_deep_copy) {}
    virtual ~dag_accumulator() {}

    virtual void transform(pasched::schedule_dag& dag, const pasched::scheduler& s, pasched::schedule_chain& c,
        pasched::transformation_status& status) const
    {
        status.begin_transformation();
        status.set_modified_graph(false);
        status.set_junction(false);
        status.set_deadlock(false);

        pasched::schedule_dag *d = m_deep ? dag.deep_dup() : dag.dup();
        /* accumulate */
        m_dag.add_units(d->get_units());
        m_dag.add_dependencies(d->get_deps());
        delete d;
        /* forward */
        s.schedule(dag, c);

        status.end_transformation();
    }

    pasched::schedule_dag& get_dag() { return m_dag; }

    protected:
    /* Little hack here. A transformation is not supposed to keep internal state but here
     * we want to accumulate DAGs scheduled so we keep a mutable var */
    mutable pasched::generic_schedule_dag m_dag;
    bool m_deep;
};

/**
 * Few tables for passes registration and blabla
 */
typedef void (*read_cb_t)(const char *filename, pasched::schedule_dag& dag);
typedef void (*write_cb_t)(pasched::schedule_dag& dag, const char *filename);

struct format_t
{
    const char *name;
    const char **ext;
    const char *desc;
    read_cb_t read;
    write_cb_t write;
};

const char *ddl_ext[] = {"ddl", 0};
const char *dot_ext[] = {"dot", 0};
const char *dotsvg_ext[] = {"svg", 0};
const char *null_ext[] = {0};

format_t formats[] =
{
    {"ddl", ddl_ext, "Data Dependency Language file", &ddl_read, 0},
    {"lsd", ddl_ext, "LLVM Schedule DAG file", &lsd_read, &lsd_write},
    {"dot", dot_ext, "Graphviz file", 0, &dot_write},
    {"dotsvg", dotsvg_ext, "Graphviz file rendered to SVG", 0, &dotsvg_write},
    {"null", null_ext, "Drop output to the void", 0, &null_write},
    {0, 0, 0, 0, 0}
};

/**
 * Helper function to display help
 */
void display_usage()
{
    std::cout << "usage: driver <fmt> <input> <fmt> <output>\n";
    std::cout << "Formats:\n";
    size_t max_size = 0;
    for(int i = 0; formats[i].name != 0; i++)
        max_size = std::max(max_size, strlen(formats[i].name));
    
    for(int i = 0; formats[i].name != 0; i++)
    {
        std::cout << " -" << std::setw(max_size) << std::left << formats[i].name << "\t" << formats[i].desc;
        if(formats[i].read == 0)
            std::cout << "(Output only)";
        if(formats[i].write == 0)
            std::cout << "(Input only)";
        std::cout << "\n";
    }
}

using pasched::time_stat;

TM_DECLARE(dtm_read, "dtm-read")
TM_DECLARE(dtm_write, "dtm-write")
TM_DECLARE(dtm_total, "dtm-total")

int __main(int argc, char **argv)
{
    TM_START(dtm_total)
    pasched::set_debug(std::cout);
    
    if(argc < 5)
    {
        display_usage();
        return 1;
    }

    int from = -1;
    int to = -1;

    for(int i = 0; formats[i].name != 0; i++)
    {
        if(strcmp(formats[i].name, argv[1] + 1) == 0)
            from = i;
        if(strcmp(formats[i].name, argv[3] + 1) == 0)
            to = i;
    }

    if(from == -1 || argv[1][0] != '-')
    {
        std::cout << "Unknown input format '" << argv[1] << "'\n";
        return 1;
    }

    if(formats[from].read == 0)
    {
        std::cout << "Format '" << formats[from].name << "' cannot be used as input\n";
        return 1;
    }

    if(to == -1 || argv[3][0] != '-')
    {
        std::cout << "Unknown output format '" << argv[3] << "'\n";
        return 1;
    }

    if(formats[to].write == 0)
    {
        std::cout << "Format '" << formats[to].name << "' cannot be used as output\n";
        return 1;
    }

    /* read DAG */
    TM_START(dtm_read)
    pasched::generic_schedule_dag dag;
    formats[from].read(argv[2], dag);
    if(!dag.is_consistent())
    {
        std::cout << "Internal error, schedule DAG is not consistent !\n";
        return 1;
    }
    TM_STOP(dtm_read)

    std::cout << "#nodes: " << dag.get_units().size() << "\n";
    std::cout << "#deps: " << dag.get_deps().size() << "\n";

    pasched::transformation_pipeline pipeline;
    pasched::transformation_pipeline snd_stage_pipe;
    pasched::transformation_loop loop(&snd_stage_pipe);
    /* don't do deep copy, otherwise, the unit won't have the same address in
     * the chain and in the DAG ! */
    dag_accumulator after_unique_accum(false);
    dag_accumulator accumulator;
    pipeline.add_stage(new pasched::unique_reg_ids);
    pipeline.add_stage(&after_unique_accum);
    pipeline.add_stage(&loop);
    pipeline.add_stage(&accumulator);
    
    //snd_stage_pipe.add_stage(new pasched::strip_dataless_units);
    //snd_stage_pipe.add_stage(new pasched::strip_useless_order_deps);
    //snd_stage_pipe.add_stage(new pasched::simplify_order_cuts);
    snd_stage_pipe.add_stage(new pasched::handle_physical_regs);
    //snd_stage_pipe.add_stage(new pasched::split_def_use_dom_use_deps);
    //snd_stage_pipe.add_stage(new pasched::smart_fuse_two_units(false, true));
    //snd_stage_pipe.add_stage(new pasched::break_symmetrical_branch_merge);
    //snd_stage_pipe.add_stage(new pasched::collapse_chains);
    //snd_stage_pipe.add_stage(new pasched::split_merge_branch_units);

    #if 0
    pasched::simple_rp_scheduler basic_sched;
    pasched::mris_ilp_scheduler sched(&basic_sched, 1000, true);
    #elif 1
    pasched::simple_rp_scheduler basic_sched;
    pasched::exp_scheduler sched(&basic_sched, 5000, false);
    #else
    pasched::simple_rp_scheduler sched;
    #endif
    
    pasched::generic_schedule_chain chain;
    pasched::basic_status status;
    /* rememember dag for later check */
    pasched::schedule_dag *dag_copy = dag.dup();
    pipeline.transform(dag, sched, chain, status);
    /* check chain against dag */
    bool ok = chain.check_against_dag(after_unique_accum.get_dag());
    if(!ok)
        throw std::runtime_error("invalid schedule");
    /* we don't want to run it on the initial graph because it will be a disaster
     * if the reg id have not been uniqued. That why we capture the dag just after
     * reg renaming */
    std::cout << "RP=" << chain.compute_rp_against_dag(after_unique_accum.get_dag()) << "\n";
    delete dag_copy;
    //debug_view_dag(accumulator.get_dag());
    //debug_view_chain(chain);
    //debug_view_scheduled_dag(after_unique_accum.get_dag(), chain);

    /*
    std::cout << "Status: Mod=" << status.has_modified_graph() << " Junction=" << status.is_junction()
            << " Deadlock=" << status.is_deadlock() << "\n";
    */
    //std::cout << "Trivial-DAGs-scheduled: " << statistics.m_trivials << "/" << statistics.m_count << "\n";

    TM_START(dtm_write)
    formats[to].write(accumulator.get_dag(), argv[4]);
    TM_STOP(dtm_write)

    TM_STOP(dtm_total)

    #if 0
    if(pasched::time_stat::get_time_stat_count() != 0)
    {
        std::cout << "Time statistics:\n";
        for(size_t i = 0; i < pasched::time_stat::get_time_stat_count(); i++)
        {
            pasched::time_stat *ts = pasched::time_stat::get_time_stat_by_index(i);
            double time = (double)ts->get_timer().get_value() / (double)ts->get_timer().get_hz();
            
            std::cout << "  " << ts->get_name() << ": " << time << " sec\n";
        }
    }
    
    #endif
    
    return 0;
}

int main(int argc, char **argv)
{
    try
    {
        return __main(argc, argv);
    }
    catch(std::exception& e)
    {
        std::cout << "exception: " << e.what() << "\n";
        return 1;
    }
    catch(...)
    {
        std::cout << "exception: unknown !\n";
        return 1;
    }
}
