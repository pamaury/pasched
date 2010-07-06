#ifndef __PAMAURY_SCHED_DAG_VIEWER_HPP__
#define __PAMAURY_SCHED_DAG_VIEWER_HPP__

#include "sched-dag.hpp"

namespace PAMAURY_SCHEDULER_NS
{

void debug_view_dag(const schedule_dag& dag,
    const std::vector< dag_printer_opt >& opts = std::vector< dag_printer_opt >());

}

#endif /* __PAMAURY_SCHED_DAG_VIEWER_HPP__ */
