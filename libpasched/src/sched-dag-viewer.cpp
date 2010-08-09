#include "sched-dag-viewer.hpp"
#include "tools.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cassert>

namespace PAMAURY_SCHEDULER_NS
{

void debug_view_dag(const schedule_dag& dag,
    const std::vector< dag_printer_opt >& opts)
{
    std::string dot_name(tmpnam(NULL));
    dump_schedule_dag_to_dot_file(dag, dot_name.c_str(), opts);
    std::string svg_name(tmpnam(NULL));
    #if 1
    system((std::string("dot -Tps -o ") + svg_name + " " + dot_name + " && gv " + svg_name).c_str());
    #else
    system(("dotty " + dot_name).c_str());
    #endif
    remove(dot_name.c_str());
    remove(svg_name.c_str());
}

void debug_view_dag(const schedule_dag& dag)
{
    debug_view_dag(dag, std::vector< dag_printer_opt >());
}

void debug_view_chain(const schedule_chain& chain,
    const std::vector< dag_printer_opt >& opts)
{
    generic_schedule_dag dag;
    for(size_t i = 0; i < chain.get_unit_count(); i++)
    {
        dag.add_unit(chain.get_unit_at(i));
        if(i > 0)
        {
            schedule_dep d;
            d.set_from(chain.get_unit_at(i - 1));
            d.set_to(chain.get_unit_at(i));
            d.set_kind(schedule_dep::order_dep);
            dag.add_dependency(d);
        }
    }

    debug_view_dag(dag, opts);
}

void debug_view_chain(const schedule_chain& c)
{
    debug_view_chain(c, std::vector< dag_printer_opt >());
}

void debug_view_scheduled_dag(const schedule_dag& _dag, const schedule_chain& chain, const std::vector< dag_printer_opt >& _opts)
{
    schedule_dag *dag = _dag.dup();
    std::vector< dag_printer_opt > opts(_opts);
    /* just print the dag augmented with edges to force scheduled order, with a different color */
    for(size_t i = 0; (i + 1) < chain.get_unit_count(); i++)
    {
        schedule_dep dep = schedule_dep(
            chain.get_unit_at(i),
            chain.get_unit_at(i + 1),
            schedule_dep::order_dep);
        dag->add_dependency(dep);
        dag_printer_opt o;
        o.type = dag_printer_opt::po_color_dep;
        o.color_dep.dep = dep;
        o.color_dep.color = "green";
        o.color_dep.style = "bold";
        o.color_dep.match_all = false;
        opts.push_back(o);
    }
    debug_view_dag(*dag, opts);
    delete dag;
}

void debug_view_scheduled_dag(const schedule_dag& dag, const schedule_chain& chain)
{
    debug_view_scheduled_dag(dag, chain, std::vector< dag_printer_opt >());
}

}
