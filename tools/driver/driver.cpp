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
    dag_accumulator() {}
    virtual ~dag_accumulator() {}

    virtual void transform(pasched::schedule_dag& dag, const pasched::scheduler& s, pasched::schedule_chain& c,
        pasched::transformation_status& status) const
    {
        status.begin_transformation();
        status.set_modified_graph(false);
        status.set_junction(false);
        status.set_deadlock(false);
        
        pasched::schedule_dag *d = dag.deep_dup();
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
};

class dag_statistics : public pasched::transformation
{
    public:
    dag_statistics() :m_count(0), m_trivials(0) { }
    virtual ~dag_statistics() {}

    virtual void transform(pasched::schedule_dag& dag, const pasched::scheduler& s, pasched::schedule_chain& c,
        pasched::transformation_status& status) const
    {
        status.begin_transformation();
        status.set_modified_graph(false);
        status.set_junction(false);
        status.set_deadlock(false);
        
        m_count++;
        if(dag.get_units().size() <= 1)
            m_trivials++;
        /* forward */
        s.schedule(dag, c);

        status.end_transformation();
    }

    public:
    mutable size_t m_count;
    mutable size_t m_trivials;
};

class trivial_dag_remover : public pasched::transformation
{
    public:
    trivial_dag_remover() { }
    virtual ~trivial_dag_remover() {}

    virtual void transform(pasched::schedule_dag& dag, const pasched::scheduler& s, pasched::schedule_chain& c,
        pasched::transformation_status& status) const
    {
        status.begin_transformation();
        status.set_modified_graph(false);
        status.set_junction(false);

        if(dag.get_units().size() <= 1)
        {
            status.set_deadlock(true);
            if(dag.get_units().size() == 1)
                c.append_unit(dag.get_units()[0]);
        }
        else
        {
            status.set_deadlock(false);
            s.schedule(dag, c);
        }

        status.end_transformation();
    }

    public:
};

const pasched::transformation *simplify_order_cuts()
{
    return new pasched::simplify_order_cuts;
}

/**
 * If the graph consist of two subgraphs G and H with one articulation node x
 * such that each node in G has a directed path to x and each node y in H
 * has a directed path from x, then we can safely schedule G then H because
 * any schedule node in H reauires x to be schedule which thus require that
 * G be scheduled a whole. So we split x and the graph into two subgraphs
 */
void split_merge_branch_units_conservative(pasched::schedule_dag& dag)
{
    sched_unit_vec_t to_split;

    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        if(dag.get_preds(dag.get_units()[u]).size() == 0 ||
                dag.get_succs(dag.get_units()[u]).size() == 0)
            continue;
        
        sched_unit_set_t pr = dag.get_reachable(dag.get_units()[u], sched_dag_t::rf_follow_preds);
        sched_unit_set_t sr = dag.get_reachable(dag.get_units()[u], sched_dag_t::rf_follow_succs);

        if((sr.size() + pr.size() + 1) != dag.get_units().size())
            continue;

        sched_unit_set_t::iterator it = pr.begin();

        for(; it != pr.end(); ++it)
        {
            for(size_t i = 0; i < dag.get_succs(*it).size(); i++)
                if(sr.find(dag.get_succs(*it)[i].to()) != sr.end())
                    goto Lskip;
        }

        it = sr.begin();
        for(; it != sr.end(); ++it)
        {
            for(size_t i = 0; i < dag.get_preds(*it).size(); i++)
                if(pr.find(dag.get_preds(*it)[i].from()) != pr.end())
                    goto Lskip;
        }

        //std::cout << "found split node: " << dag.get_units()[u]->to_string() << "\n";
        //std::cout << "pr: " << pr << "\n";
        //std::cout << "sr: " << sr << "\n";

        to_split.push_back(dag.get_units()[u]);

        Lskip:
        continue;
    }

    for(size_t u = 0; u < to_split.size(); u++)
    {
        sched_unit_ptr_t unit = to_split[u];
        // make a copy of the unit
        sched_unit_ptr_t cpy = unit->dup();
        // add the unit
        dag.add_unit(cpy);
        // copy succs dependencies to the unit copy
        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            sched_dep_t d = dag.get_succs(unit)[i];
            d.set_from(cpy);
            dag.add_dependency(d);
        }
        // remove those dependencies from the original unit
        while(dag.get_succs(unit).size() != 0)
            dag.remove_dependency(dag.get_succs(unit)[0]);
    }
}

/**
 * If there is a node U such that all children for the same dependency
 * are U(1) ... U(k) satisfy the property that all U(i) are reachable
 * from, say, U(1), then we can cut all (U,U(i)) dep for i>=1
 * and add (U(1),U(i)) instead
 */
const pasched::transformation *split_def_use_dom_use_deps()
{
    return new pasched::split_def_use_dom_use_deps;
}

/**
 * Run the alternative MRIS ILP scheduler on the graph and adds the order links
 * in order to force a chain order.
 */
void mris_ilp_schedule(pasched::schedule_dag& dag)
{
    pasched::mris_ilp_scheduler mris;
    pasched::generic_schedule_chain sc;

    mris.schedule(dag, sc);

    //std::cout << "-==== Schedule ====-\n";
    /*
    for(size_t i = 0; i < sc.get_units().size(); i++)
        std::cout << "(" << std::setw(2) << i << ") " << sc.get_units()[i]->to_string() << "\n";
    */
    /* add order edges to force chain order in the original graph */
    for(size_t i = 0; (i + 1) < sc.get_units().size(); i++)
    {
        sched_dep_t d;
        d.set_from(sc.get_units()[i]);
        d.set_to(sc.get_units()[i + 1]);
        d.set_kind(sched_dep_t::order_dep);
        dag.add_dependency(d);
    }
}

const pasched::transformation *unique_reg_ids()
{
    return new pasched::unique_reg_ids;
}

const pasched::transformation *strip_useless_order_deps()
{
    return new pasched::strip_useless_order_deps;
}

const pasched::transformation *smart_fuse_two_units(bool aggressive)
{
    return new pasched::smart_fuse_two_units(aggressive, false);
}

const pasched::transformation *smart_fuse_two_units_conservative()
{
    return smart_fuse_two_units(false);
}

const pasched::transformation *smart_fuse_two_units_aggressive()
{
    return smart_fuse_two_units(true);
}

/**
 * 
 */
void strip_nrinro_costless_units(pasched::schedule_dag& dag)
{
    /* We repeat everything from the beginning at each step because
     * each modification can add order dependencies and delete a node,
     * so it's easier this way */
    while(true)
    {
        /* Find such a node */
        size_t u;
        sched_unit_ptr_t unit = 0;
        for(u = 0; u < dag.get_units().size(); u++)
        {
            unit = dag.get_units()[u];
            /* - costless */
            if(unit->internal_register_pressure() != 0)
                goto Lskip;
            /* - NRI */
            for(size_t i = 0; i < dag.get_preds(unit).size(); i++)
                if(dag.get_preds(unit)[i].kind() != sched_dep_t::order_dep)
                    goto Lskip;

            /* - NRO */
            for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
                if(dag.get_succs(unit)[i].kind() != sched_dep_t::order_dep)
                    goto Lskip;
            break;
            Lskip:
            continue;
        }

        /* Stop if not found */
        if(u == dag.get_units().size())
            break;
        /* We will remove the unit but add some dependencies to keep correctness
         * We add the dependencies at the end because it can invalidate
         * the lists we are going through */
        sched_dep_vec_t to_add;
        
        for(size_t i = 0; i < dag.get_preds(unit).size(); i++)
            for(size_t j = 0; j < dag.get_succs(unit).size(); j++)
            {
                sched_dep_t d;
                d.set_from(dag.get_preds(unit)[i].from());
                d.set_to(dag.get_succs(unit)[j].to());
                d.set_kind(sched_dep_t::order_dep);

                to_add.push_back(d);
            }

        /* Add the dependencies */
        dag.add_dependencies(to_add);

        /* Remove the unit */
        dag.remove_unit(unit);
    }
}

void strip_redundant_data_deps(pasched::schedule_dag& dag)
{
    sched_dep_vec_t to_remove;
    
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        sched_unit_ptr_t unit = dag.get_units()[u];
        for(size_t i = 0; i < dag.get_succs(unit).size(); i++)
        {
            const sched_dep_t& dep = dag.get_succs(unit)[i];
            if(dep.kind() != sched_dep_t::data_dep)
                continue;
            for(size_t j = i + 1; j < dag.get_succs(unit).size(); j++)
            {
                const sched_dep_t& dep2 = dag.get_succs(unit)[j];
                if(dep2.kind() == sched_dep_t::data_dep &&
                        dep2.to() == dep.to() &&
                        dep2.reg() == dep2.reg())
                    to_remove.push_back(dep2);
            }
        }
    }

    dag.remove_dependencies(to_remove);
}

const pasched::transformation *break_symmetrical_branch_merge()
{
    return new pasched::break_symmetrical_branch_merge;
}

void reg_analysis_info(pasched::schedule_dag& dag)
{
    std::cout << "--== Reg analysis ==--\n";
    for(size_t u = 0; u < dag.get_units().size(); u++)
    {
        sched_unit_ptr_t unit = dag.get_units()[u];
        std::cout << "  Unit: " << unit << "\n";
        sched_reg_set_t vc = dag.get_reg_create(unit);
        sched_reg_set_t vu = dag.get_reg_use(unit);
        sched_reg_set_t vd = dag.get_reg_destroy(unit);
        sched_reg_set_t vde = dag.get_reg_destroy_exact(unit);
        sched_reg_set_t vdde = dag.get_reg_dont_destroy_exact(unit);

        std::cout << "    VC  =" << vc << "\n";
        std::cout << "    VU  =" << vu << "\n";
        std::cout << "    VD  =" << vd << "\n";
        std::cout << "    VDE =" << vde << "\n";
        std::cout << "    VDDE=" << vdde << "\n";
    }
}

/**
 * Few tables for passes registration and blabla
 */
typedef void (*read_cb_t)(const char *filename, pasched::schedule_dag& dag);
typedef void (*write_cb_t)(pasched::schedule_dag& dag, const char *filename);
typedef const pasched::transformation * (*pass_factory_t)();

struct format_t
{
    const char *name;
    const char **ext;
    const char *desc;
    read_cb_t read;
    write_cb_t write;
};

struct pass_t
{
    const char *name;
    const char *desc;
    pass_factory_t factory;
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

pass_t passes[] =
{
    {"unique-reg-ids", "Number data dependencies to have unique register IDs", &unique_reg_ids},
    //{"strip-nrinro-costless-units", "Strip no-register-in no-register-out costless units", &strip_nrinro_costless_units},
    {"strip-useless-order-deps", "Strip order dependencies already enforced by order depedencies", &strip_useless_order_deps},
    {"simplify-order-cuts", "Simplify the graph by finding one way cuts made of order dependencies", &simplify_order_cuts},
    //{"mris-ilp-schedule", "Schedule it with the MRIS ILP scheduler", &mris_ilp_schedule},
    {"split-def-use-dom-use-deps", "Split edges from a def to a use which dominates all other uses", &split_def_use_dom_use_deps},
    //{"strip-redundant-data-deps", "", &strip_redundant_data_deps},
    {"smart-fuse-two-units-conservative", "", &smart_fuse_two_units_conservative},
    {"smart-fuse-two-units-aggressive", "", &smart_fuse_two_units_aggressive},
    {"break-symmetrical-branch-merge", "", &break_symmetrical_branch_merge},
    //{"reg-analysis-info", "", &reg_analysis_info},
    {0, 0, 0}
};

/**
 * Helper function to display help
 */
void display_usage()
{
    std::cout << "usage: driver <fmt> <input> <fmt> <output> [<pass 1> <pass 2> ...]\n";
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
    std::cout << "Passes:\n";

    max_size = 0;
    for(int i = 0; passes[i].name != 0; i++)
        max_size = std::max(max_size, strlen(passes[i].name));
    for(int i = 0; passes[i].name != 0; i++)
    {
        std::cout << " -" << std::setw(max_size) << std::left << passes[i].name << "\t" << passes[i].desc << "\n";
    }
}

int __main(int argc, char **argv)
{
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
    pasched::generic_schedule_dag dag;
    formats[from].read(argv[2], dag);
    if(!dag.is_consistent())
    {
        std::cout << "Internal error, schedule DAG is not consistent !\n";
        return 1;
    }

    #if 0
    
    pasched::transformation_pipeline pipeline;
    /* build pipeline */
    for(int i = 5; i < argc; i++)
    {
        int j = 0;
        while(passes[j].name && strcmp(passes[j].name, argv[i] + 1) != 0)
            j++;

        if(passes[j].name == 0 || argv[i][0] != '-')
        {
            std::cout << "Unknown pass '" << argv[i] << "'\n";
            return 1;
        }

        pipeline.add_stage(passes[j].factory());
    }

    /* add accumulator */
    dag_accumulator accumulator;
    pipeline.add_stage(&accumulator);

    /* run pipeline with stupid scheduler */
    pasched::basic_list_scheduler sched;
    pasched::generic_schedule_chain chain;
    pasched::basic_status status;
    pipeline.transform(dag, sched, chain, status);
    
    #else

    pasched::transformation_pipeline pipeline;
    pasched::transformation_pipeline snd_stage_pipe;
    pasched::transformation_loop loop(&snd_stage_pipe);
    dag_accumulator accumulator;
    dag_statistics statistics;
    trivial_dag_remover remover;
    pipeline.add_stage(new pasched::unique_reg_ids);
    pipeline.add_stage(&loop);
    pipeline.add_stage(&statistics);
    pipeline.add_stage(&remover);
    pipeline.add_stage(&accumulator);
    snd_stage_pipe.add_stage(new pasched::strip_dataless_units);
    snd_stage_pipe.add_stage(new pasched::strip_useless_order_deps);
    snd_stage_pipe.add_stage(new pasched::split_def_use_dom_use_deps);
    snd_stage_pipe.add_stage(new pasched::smart_fuse_two_units(false, true));
    snd_stage_pipe.add_stage(new pasched::simplify_order_cuts);
    //snd_stage_pipe.add_stage(new pasched::break_symmetrical_branch_merge);
    snd_stage_pipe.add_stage(new pasched::collapse_chains);
    snd_stage_pipe.add_stage(new pasched::split_merge_branch_units);

    pasched::basic_list_scheduler basic_sched;
    pasched::mris_ilp_scheduler sched(&basic_sched);
    pasched::generic_schedule_chain chain;
    pasched::basic_status status;
    /* rememember dag for later check */
    pasched::schedule_dag *dag_copy = dag.dup();
    pipeline.transform(dag, sched, chain, status);
    /* check chain against dag */
    bool ok = chain.check_against_dag(*dag_copy);
    delete dag_copy;
    if(!ok)
        throw std::runtime_error("invalid schedule");
    //debug_view_chain(chain);
    #endif

    /*
    std::cout << "Status: Mod=" << status.has_modified_graph() << " Junction=" << status.is_junction()
            << " Deadlock=" << status.is_deadlock() << "\n";
    */
    //std::cout << "Trivial-DAGs-scheduled: " << statistics.m_trivials << "/" << statistics.m_count << "\n";
    
    formats[to].write(accumulator.get_dag(), argv[4]);
    
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
