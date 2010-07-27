#include "sched-dag-viewer.hpp"
#include "tools.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>

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

}
