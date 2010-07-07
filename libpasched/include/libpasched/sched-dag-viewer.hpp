#ifndef __PAMAURY_SCHED_DAG_VIEWER_HPP__
#define __PAMAURY_SCHED_DAG_VIEWER_HPP__

#include "sched-dag.hpp"

namespace PAMAURY_SCHEDULER_NS
{

/**
 * Debug function which prints and render a DAG and display it in a window.
 * This is a blocking calls which will return when the window is closed.
 *
 * NOTE: this might not be available on all platforms and in all builds.
 */
void debug_view_dag(const schedule_dag& dag,
    const std::vector< dag_printer_opt >& opts = std::vector< dag_printer_opt >());

}

#endif /* __PAMAURY_SCHED_DAG_VIEWER_HPP__ */
