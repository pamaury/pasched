#ifndef __PAMAURY_SCHED_DAG_VIEWER_HPP__
#define __PAMAURY_SCHED_DAG_VIEWER_HPP__

#include "config.hpp"
#include "sched-dag.hpp"
#include "sched-chain.hpp"

namespace PAMAURY_SCHEDULER_NS
{

void debug_view_chain(const schedule_chain& dag);
void debug_view_chain(const schedule_chain& dag, const std::vector< dag_printer_opt >& opts);

/**
 * Debug function which prints and render a DAG and display it in a window.
 * This is a blocking calls which will return when the window is closed.
 *
 * NOTE: this might not be available on all platforms and in all builds.
 */
void debug_view_dag(const schedule_dag& dag);
void debug_view_dag(const schedule_dag& dag, const std::vector< dag_printer_opt >& opts);
void debug_view_scheduled_dag(const schedule_dag& dag, const schedule_chain& chain);
void debug_view_scheduled_dag(const schedule_dag& dag, const schedule_chain& chain, const std::vector< dag_printer_opt >& opts);


}

#endif /* __PAMAURY_SCHED_DAG_VIEWER_HPP__ */
