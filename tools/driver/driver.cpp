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
#include <fstream>
#include <algorithm>

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

void dot_write(const pasched::schedule_dag& dag, const char *filename, const std::vector< pasched::dag_printer_opt >& opts)
{
    pasched::dump_schedule_dag_to_dot_file(dag, filename, opts);
}

void dotsvg_write(const pasched::schedule_dag& dag, const char *filename, const std::vector< pasched::dag_printer_opt >& opts)
{
    char *name = tmpnam(NULL);
    pasched::dump_schedule_dag_to_dot_file(dag, name, opts);
    system((std::string("dot -Tsvg -o ") + filename + " " + name).c_str());
    remove(name);
}

void dotpdf_write(const pasched::schedule_dag& dag, const char *filename, const std::vector< pasched::dag_printer_opt >& opts)
{
    char *name = tmpnam(NULL);
    pasched::dump_schedule_dag_to_dot_file(dag, name, opts);
    system((std::string("dot -Tpdf -o ") + filename + " " + name).c_str());
    remove(name);
}

void lsd_write(const pasched::schedule_dag& dag, const char *filename, const std::vector< pasched::dag_printer_opt >& opts)
{
    pasched::dump_schedule_dag_to_lsd_file(dag, filename);
}

void null_write(const pasched::schedule_dag& dag, const char *filename, const std::vector< pasched::dag_printer_opt >& opts)
{
    (void) dag;
    (void) filename;
    (void) opts;
}

std::string tex_escape_string(const std::string& str)
{
    std::ostringstream oss;

    for(size_t i = 0; i < str.size(); i++)
    {
        char c = str[i];
        if(c == '\\')
            oss << "\\backslash ";
        else if(c == '<' && (i + 1) < str.size() && str[i + 1] == '-')
        {
            oss << "\\leftarrow ";
            i++;
        }
        else if(c == '<')
            oss << "\\langle ";
        else if(c == '>')
            oss << "\\rangle ";
        else if(c == '_')
            oss << "\\_";
        else if(c == '%')
            oss << "\\%";
        else if(c == ' ')
            oss << "\\thinspace ";
        else
            oss << c;
    }

    return oss.str();
}

std::string encode(size_t i)
{
    if(i == 0)
        return "z";
    std::string s;
    while(i != 0)
    {
        s += 'a' + i % 10;
        i /= 10;
    }
    return s;
}

std::string render_instruction(std::string str)
{
    std::string text;
    if(str.find("\n") != std::string::npos)
    {
        text = "$\\begin{array}{@{}l@{}}";
        str = tex_escape_string(str);
        size_t pos = str.find("\n");
        while(pos != std::string::npos)
        {
            str.replace(pos, 1, "\\\\");
            pos = str.find("\n");
        }
        
        text += str;
        text += "\\end{array}$";
    }
    else
        text = std::string("$") + tex_escape_string(str) + "$";
    return text;
}

void chain_analysis_write(const pasched::schedule_dag& dag, const pasched::schedule_chain& chain,
    const char *filename, const std::vector< pasched::dag_printer_opt >& opts)
{
    std::ofstream fout(filename);

    size_t rp = chain.compute_rp_against_dag(dag);
    
    fout << "\\documentclass{article}\n";
    fout << "\\usepackage{amsmath,amssymb,mathrsfs}\n";
    fout << "\\usepackage{color}\n";
    fout << "\\usepackage[utf8]{inputenc}\n";
    fout << "\\usepackage[T1]{fontenc}\n";
    fout << "\\usepackage[english]{babel}\n";
    fout << "\\usepackage{multirow}\n";
    fout << "\\usepackage{colortbl}\n";
    fout << "\\usepackage{calc}\n";
    fout << "\\thispagestyle{empty}\n";
    fout << "\\begin{document}\n";

    std::map< pasched::schedule_dep::reg_t, size_t > reg_use_left;
    std::map< pasched::schedule_dep::reg_t, size_t > reg_map;
    std::map< size_t, pasched::schedule_dep::reg_t > rev_reg_map;
    std::map< size_t, bool > color_switch;

    const std::string __color[2] = {"red", "green"};
    const std::string irp_color = "blue";

    fout << "\\begin{tabular}{l";
    for(size_t i = 0; i < rp; i++)
        fout << "c@{}";
    fout <<"}\n";
    for(size_t i = 0; i < chain.get_unit_count(); i++)
    {
        const pasched::schedule_unit *unit = chain.get_unit_at(i);
        
        std::vector< bool > alive[2];
        std::vector< std::string > color[2];
        /* analysis */
        for(size_t j = 0; j < rp; j++)
        {
            alive[0].push_back(rev_reg_map.find(j) != rev_reg_map.end());
            color[0].push_back(__color[color_switch[j]]);
        }
        /* printing */
        std::string str = chain.get_unit_at(i)->to_string();
        size_t nb_lines = std::count(str.begin(), str.end(), '\n') + 1;
        size_t irp = chain.get_unit_at(i)->internal_register_pressure();
        size_t real_nb_lines;

        if(irp == 0)
            real_nb_lines = nb_lines + (nb_lines % 2); /* have even number of lines */
        else
            real_nb_lines = std::max(size_t(3), nb_lines + 1 - (nb_lines % 2)); /* have odd number of lines */

        fout << "\\multirow{" << real_nb_lines << "}*{" << render_instruction(str) << "}";

        for(size_t rep = 0; rep < real_nb_lines / 2; rep++)
        {
            for(size_t j = 0; j < rp; j++)
            {
                fout << "&";
                if(!alive[0][j])
                    continue;
                fout << "\\multicolumn{1}{@{}>{\\columncolor{" << color[0][j] << "}[0pt]}m{1ex}@{}}{}\\enskip";
            }
            fout << "\\\\\n";
        }
        /* kill registers */
        for(size_t j = 0; j < dag.get_preds(unit).size(); j++)
        {
            const pasched::schedule_dep& dep = dag.get_preds(unit)[j];
            if(!dep.is_data())
                continue;
            assert(reg_map.find(dep.reg()) != reg_map.end());
            assert(reg_use_left.find(dep.reg()) != reg_use_left.end());
            assert(rev_reg_map.find(reg_map[dep.reg()]) != rev_reg_map.end());
            if((--reg_use_left[dep.reg()]) == 0)
            {
                reg_use_left.erase(dep.reg());
                rev_reg_map.erase(reg_map[dep.reg()]);
                reg_map.erase(dep.reg());
            }
        }
        /* IRP + printing */
        if(irp != 0)
        {
            /* allocate internal registers */
            std::vector< size_t > added;
            for(size_t j = 0; j < rp && irp > 0; j++)
            {
                if(rev_reg_map.find(j) == rev_reg_map.end())
                {
                    rev_reg_map[j] = 0;
                    added.push_back(j);
                    alive[0][j] = true;
                    color[0][j] = irp_color;
                    irp--;
                }
            }
            /* printing */
            for(size_t j = 0; j < rp; j++)
            {
                fout << "&";
                if(!alive[0][j])
                    continue;
                fout << "\\multicolumn{1}{@{}>{\\columncolor{" << color[0][j] << "}[0pt]}m{1ex}@{}}{}\\enskip";
            }
            fout << "\\\\\n";
            /* cleanup */
            for(size_t j = 0; j < added.size(); j++)
                rev_reg_map.erase(added[j]);
        }
        /* create registers */
        for(size_t j = 0; j < dag.get_succs(unit).size(); j++)
        {
            const pasched::schedule_dep& dep = dag.get_succs(unit)[j];
            if(!dep.is_data())
                continue;
            if((++reg_use_left[dep.reg()]) == 1)
            {
                for(size_t j = 0; j < rp; j++)
                    if(rev_reg_map.find(j) == rev_reg_map.end())
                    {
                        reg_map[dep.reg()] = j;
                        rev_reg_map[j] = dep.reg();
                        color_switch[j] = !color_switch[j];
                        break;
                    }
            }
        }
        /* analysis */
        for(size_t j = 0; j < rp; j++)
        {
            alive[1].push_back(rev_reg_map.find(j) != rev_reg_map.end());
            color[1].push_back(__color[color_switch[j]]);
        }
        /* printing */
        for(size_t rep = 0; rep < real_nb_lines / 2; rep++)
        {
            for(size_t j = 0; j < rp; j++)
            {
                fout << "&";
                if(!alive[1][j])
                    continue;
                fout << "\\multicolumn{1}{@{}>{\\columncolor{" << color[1][j] << "}[0pt]}m{1ex}@{}}{}\\enskip";
            }
            fout << "\\\\\n";
        }
        fout << "\\hline";
    }
    fout << "\\end{tabular}\n";
    fout << "\\end{document}\n";
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
typedef void (*write_cb_t)(const pasched::schedule_dag& dag, const char *filename,
    const std::vector< pasched::dag_printer_opt >& opts);
typedef void (*chain_write_cb_t)(const pasched::schedule_dag& dag, const pasched::schedule_chain& chain,
    const char *filename, const std::vector< pasched::dag_printer_opt >& opts);

struct format_t
{
    const char *name;
    const char **ext;
    const char *desc;
    read_cb_t read;
    write_cb_t write;
    chain_write_cb_t chain_write;
};

const char *ddl_ext[] = {"ddl", 0};
const char *dot_ext[] = {"dot", 0};
const char *dotsvg_ext[] = {"svg", 0};
const char *dotpdf_ext[] = {"pdf", 0};
const char *null_ext[] = {0};
const char *analysis_ext[] = {0};

format_t formats[] =
{
    {"ddl", ddl_ext, "Data Dependency Language file", &ddl_read, 0, 0},
    {"lsd", ddl_ext, "LLVM Schedule DAG file", &lsd_read, &lsd_write, 0},
    {"dot", dot_ext, "Graphviz file", 0, &dot_write, 0},
    {"dotsvg", dotsvg_ext, "Graphviz file rendered to SVG", 0, &dotsvg_write, 0},
    {"dotpdf", dotpdf_ext, "Graphviz file rendered to PDF", 0, &dotpdf_write, 0},
    {"null", null_ext, "Drop output to the void", 0, &null_write, 0},
    {"analysis", analysis_ext, "Live analysis of the resulting schedule in Tex", 0, 0, &chain_analysis_write},
    {0, 0, 0, 0, 0, 0}
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

    if(formats[to].write == 0 && formats[to].chain_write == 0)
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
    pipeline.add_stage(&accumulator);
    pipeline.add_stage(&loop);
    //pipeline.add_stage(&accumulator);
    
    snd_stage_pipe.add_stage(new pasched::strip_dataless_units);
    snd_stage_pipe.add_stage(new pasched::strip_useless_order_deps);
    snd_stage_pipe.add_stage(new pasched::simplify_order_cuts);
    snd_stage_pipe.add_stage(new pasched::handle_physical_regs);
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
    std::vector< pasched::dag_printer_opt > opts;
    {
        pasched::dag_printer_opt o;
        o.type = pasched::dag_printer_opt::po_hide_dep_labels;
        o.hide_dep_labels.hide_order = true;
        o.hide_dep_labels.hide_virt = false;
        o.hide_dep_labels.hide_phys = false;
        opts.push_back(o);
    }
    if(formats[to].write != 0)
        formats[to].write(accumulator.get_dag(), argv[4], opts);
    if(formats[to].chain_write != 0)
        formats[to].chain_write(after_unique_accum.get_dag(), chain, argv[4], opts);
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
