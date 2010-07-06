#include "sched-dag-viewer.hpp"
#include "tools.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>

//#define AUTO_CHECK_CONSISTENCY

namespace PAMAURY_SCHEDULER_NS
{

void debug_view_dag(const schedule_dag& dag,
    const std::vector< dag_printer_opt >& opts)
{
    std::string dot_name(tmpnam(NULL));
    dump_schedule_dag_to_dot_file(dag, dot_name.c_str(), opts);
    std::string svg_name(tmpnam(NULL));
    system((std::string("dot -Tps -o ") + svg_name + " " + dot_name + " && gv " + svg_name).c_str());
    remove(dot_name.c_str());
    remove(svg_name.c_str());
}

}
