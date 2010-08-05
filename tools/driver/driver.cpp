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

class handle_physical_regs : public pasched::transformation
{
    public:
    /**
     * promote_phys_to_virt means that when a physical register is proved to have
     * the same semantics has a virtual one (that is when it can't overlap another
     * physical register with the same ID) then it is promoted to a virtual register.
     * This is useful because some code might not handle physical regs so it would be
     * stupid to skip optmizations is cases where it doesn't change anything */
    handle_physical_regs(bool promote_phys_to_virt = true) : m_promote(promote_phys_to_virt) {}
    virtual ~handle_physical_regs() {}

    virtual void transform(pasched::schedule_dag& dag, const pasched::scheduler& s, pasched::schedule_chain& c,
        pasched::transformation_status& status) const
    {
        typedef std::vector< std::pair< const pasched::schedule_unit *, const pasched::schedule_unit * > > reg_problem_list;
        typedef std::map< pasched::schedule_dep::reg_t, reg_problem_list > reg_problem_map;
        typedef std::map< pasched::schedule_dep::reg_t, std::set< const pasched::schedule_unit * > > reg_creator_list;
        bool modified = false;
        reg_problem_map problems;

        /**
         * This transformation is organized as follow:
         * 1) build a map of physical registers creators
         * 2) skip work if no register has no more than one creator
         * 3) build an interferance map of creators and use a path map of the graph
         *    to reduce it
         * 4) skip work if there is no actual problem
         * 5) for each problem, check if there is a natural partial order between creators
         *    then can be strengthened to solve it and do it
         * 6) skip any further work if the problem list if empty
         * 7) ...
         *
         * Note that the checks are more expensive each time because there are actually only
         * a few relevant cases so we try to minimize the work in most cases */
        
        status.begin_transformation();

        debug_view_dag(dag);

        /* build a map of physical registers creators */
        reg_creator_list reg_creators;
        std::vector< pasched::schedule_dep::reg_t > several_creators;
        for(size_t i = 0; i < dag.get_deps().size(); i++)
            if(dag.get_deps()[i].is_phys())
            {
                reg_creators[dag.get_deps()[i].reg()].insert(dag.get_deps()[i].from());
                if(reg_creators[dag.get_deps()[i].reg()].size() == 2)
                    several_creators.push_back(dag.get_deps()[i].reg());
            }
        
        /* If no physical register has several creators, just promote them all and go on */
        if(several_creators.size() == 0)
            goto Lpromote_all;

        /* We are here because at least one physical register used twice or more.
         * Hopefully, in a number of cases, this is a false positive because the
         * natural ordering will prevent them from interfering.
         * Check this for each register.
         * Loop because we add edges for detected partial order which can change things */
        while(true)
        {
            reg_problem_map partials;
            /* create path map */
            std::vector< std::vector< bool > > path;
            std::map< const pasched::schedule_unit *, size_t> name_map;
            dag.build_path_map(path, name_map);
            
            for(size_t i = 0; i < several_creators.size(); i++)
            {
                pasched::schedule_dep::reg_t reg = several_creators[i];
                std::vector< const pasched::schedule_unit * > creators = pasched::set_to_vector(reg_creators[reg]);
                
                /* Create interferance map */
                /* C interfere with C' if it's possible to schedule C, C' and their use such has the
                 * same physical register is alive and created by both C and C' at the same time
                 *
                 * C has partial order with C' if the dependencies suggest that C and its uses must be
                 * schedule before C' but we need to add more dependencies to enforce that */
                std::vector< std::vector< bool > > interfere;
                std::vector< std::vector< bool > > partial_order;
                interfere.resize(creators.size());
                partial_order.resize(creators.size());
                for(size_t i = 0; i < interfere.size(); i++)
                {
                    interfere[i].resize(creators.size(), true); /* interfere unless proven the contrary */
                    partial_order[i].resize(creators.size());
                    interfere[i][i] = false;
                }

                /* this list does not consider symmetry because if C must happen before D
                 * then obviously D happens before C so in one case, we can't conclude that
                 * they don't interfere ! */
                for(size_t i = 0; i < creators.size(); i++)
                {
                    /* consider a creator C */
                    const pasched::schedule_unit *creator = creators[i];
                    /* consider every other creator D */
                    for(size_t j = 0; j < creators.size(); j++)
                    {
                        if(i == j)
                            continue;
                        const pasched::schedule_unit *other = creators[j];
                        /* now to prove that C does not intefere with D, we must ensure that
                         * any use of the physical register created by C has a path to D
                         * So consider every succ S of C with physical reg dep */
                        bool inter = false;
                        bool partial = false;
                        for(size_t k = 0; k < dag.get_succs(other).size(); k++)
                        {
                            const pasched::schedule_dep& dep = dag.get_succs(other)[k];
                            if(!dep.is_phys() || dep.reg() != reg)
                                continue;
                            const pasched::schedule_unit *use = dep.to();
                            /* if there is no path from S to D, then they might interfere */
                            if(!path[name_map[use]][name_map[creator]])
                                inter = true;
                            /* if there is a path from C to S, then it means that C has a partial order
                             * with S */
                            if(path[name_map[creator]][name_map[use]])
                                partial = true;
                        }
                        /* we proved they do not interfere */
                        if(!inter)
                        {
                            interfere[i][j] = false;
                            interfere[j][i] = false;
                        }
                        if(partial)
                            partial_order[i][j] = true;
                    }
                }

                /* Make a summary of interferance pairs */
                reg_problem_list problem_list;
                reg_problem_list partial_list;
                /* the map must be symmetrical at this point */
                for(size_t i = 0; i < creators.size(); i++)
                    for(size_t j = i + 1; j < creators.size(); j++)
                        if(interfere[i][j])
                        {
                            if(partial_order[i][j])
                                partial_list.push_back(std::make_pair(creators[i], creators[j]));
                            else if(partial_order[j][i])
                                partial_list.push_back(std::make_pair(creators[j], creators[i]));
                            else
                                problem_list.push_back(std::make_pair(creators[i], creators[j]));
                            
                        }

                if(problem_list.size() != 0)
                    problems[reg] = problem_list;
                if(partial_list.size() != 0)
                    partials[reg] = partial_list;
            }

            if(partials.size() == 0)
                break;
            /* Enforce partial orders */
            for(reg_problem_map::iterator it = partials.begin(); it != partials.end(); ++it)
            {
                reg_problem_list& l = it->second;
                for(size_t i = 0; i < l.size(); i++)
                {
                    const pasched::schedule_unit *c_from = l[i].first;
                    const pasched::schedule_unit *c_to = l[i].second;
                    /* for each successor S of C_from on register R, add a dependency
                     * from S to C_to */
                    for(size_t i = 0; i < dag.get_succs(c_from).size(); i++)
                    {
                        const pasched::schedule_dep& dep = dag.get_succs(c_from)[i];
                        if(dep.is_phys() && dep.reg() == it->first &&
                                !path[name_map[dep.to()]][name_map[c_to]])
                        {
                            dag.add_dependency(
                                pasched::schedule_dep(
                                    dep.to(), c_to, pasched::schedule_dep::order_dep));
                        }
                    }
                }
            }
            /* continue the loop */
        }
        /* If there are no more problem, just promote them all */
        if(problems.size() == 0)
            goto Lpromote_all;
        /* Ok, do a partial promoting of all creators which are not in the problem list */
        {
            /* list all (reg,creators) pairs in problem list */
            std::set< std::pair< pasched::schedule_dep::reg_t, const pasched::schedule_unit * > > problematic_pairs;
            for(reg_problem_map::iterator it = problems.begin(); it != problems.end(); ++it)
            {
                reg_problem_list& l = it->second;

                for(size_t i = 0; i < l.size(); i++)
                {
                    problematic_pairs.insert(std::make_pair(it->first, l[i].first));
                    problematic_pairs.insert(std::make_pair(it->first, l[i].second));
                }
            }
            /* list all creators and promote them if there are not in a problematic pair */
            for(reg_creator_list::iterator it = reg_creators.begin(); it != reg_creators.end(); ++it)
            {
                std::set< const pasched::schedule_unit * >::iterator it2;
                for(it2 = it->second.begin(); it2 != it->second.end(); ++it2)
                    if(problematic_pairs.find(std::make_pair(it->first, *it2)) == problematic_pairs.end())
                        promote_phys_register(dag, *it2, it->first, modified);
            }
        }
        
        /* display */
        std::cout << "Problem list:\n";
        for(reg_problem_map::iterator it = problems.begin(); it != problems.end(); ++it)
        {
            std::cout << "  Register " << it->first << "\n";
            reg_problem_list& l = it->second;
            for(size_t i = 0; i < l.size(); i++)
            {
                std::cout << "    Pair:\n";
                std::cout << "      " << l[i].first->to_string() << "\n";
                std::cout << "      " << l[i].second->to_string() << "\n";
            }
        }
        
        goto Lcont;

        Lpromote_all:
        /* promote all physical register */
        {
            for(reg_creator_list::iterator it = reg_creators.begin(); it != reg_creators.end(); ++it)
            {
                std::set< const pasched::schedule_unit * >::iterator it2;
                for(it2 = it->second.begin(); it2 != it->second.end(); ++it2)
                    promote_phys_register(dag, *it2, it->first, modified);
            }
        }

        Lcont:
        debug_view_dag(dag);
        status.set_modified_graph(modified);
        status.set_junction(false);
        status.set_deadlock(false);

        /* forward */
        s.schedule(dag, c);

        status.end_transformation();
    }

    protected:
    void promote_phys_register(
        pasched::schedule_dag& dag,
        const pasched::schedule_unit *unit,
        pasched::schedule_dep::reg_t reg,
        bool& modified) const
    {
        if(!m_promote)
            return;
        std::vector< pasched::schedule_dep > to_remove;
        std::vector< pasched::schedule_dep > to_add;
        pasched::schedule_dep::reg_t id = dag.generate_unique_reg_id();

        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            pasched::schedule_dep dep = dag.get_succs(unit)[i];
            if(!dep.is_phys() || dep.reg() != reg)
                continue;
            to_remove.push_back(dep);
            dep.set_kind(pasched::schedule_dep::virt_dep);
            dep.set_reg(id);
            to_add.push_back(dep);
        }

        dag.remove_dependencies(to_remove);
        dag.add_dependencies(to_add);
        modified = true;
    }
    
    bool m_promote;
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
    //pasched::set_debug(std::cout);
    
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

    pasched::transformation_pipeline pipeline;
    pasched::transformation_pipeline snd_stage_pipe;
    pasched::transformation_loop loop(&snd_stage_pipe);
    /* don't do deep copy, otherwise, the unit won't have the same address in
     * the chain and in the DAG ! */
    dag_accumulator after_unique_accum(false);
    dag_accumulator accumulator;
    pipeline.add_stage(new handle_physical_regs);
    pipeline.add_stage(new pasched::unique_reg_ids);
    pipeline.add_stage(&after_unique_accum);
    pipeline.add_stage(&loop);
    pipeline.add_stage(&accumulator);
    
    snd_stage_pipe.add_stage(new pasched::strip_dataless_units);
    snd_stage_pipe.add_stage(new pasched::strip_useless_order_deps);
    snd_stage_pipe.add_stage(new pasched::simplify_order_cuts);
    snd_stage_pipe.add_stage(new pasched::split_def_use_dom_use_deps);
    snd_stage_pipe.add_stage(new pasched::smart_fuse_two_units(false, true));
    snd_stage_pipe.add_stage(new pasched::break_symmetrical_branch_merge);
    snd_stage_pipe.add_stage(new pasched::collapse_chains);
    snd_stage_pipe.add_stage(new pasched::split_merge_branch_units);

    #if 0
    pasched::simple_rp_scheduler basic_sched;
    pasched::mris_ilp_scheduler sched(&basic_sched, 1000, true);
    #elif 1
    pasched::simple_rp_scheduler basic_sched;
    pasched::exp_scheduler sched(&basic_sched, 0000, false);
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
    //debug_view_dag(after_unique_accum.get_dag());
    //debug_view_chain(chain);

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
