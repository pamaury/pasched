#include "scheduler.hpp"

namespace PAMAURY_SCHEDULER_NS
{

/**
 * scheduler
 */

scheduler::scheduler()
{
}

scheduler::~scheduler()
{
}

/**
 * basic_list_scheduler
 */
basic_list_scheduler::basic_list_scheduler()
{
}

basic_list_scheduler::~basic_list_scheduler()
{
}

void basic_list_scheduler::schedule(pasched::schedule_dag& dag, pasched::schedule_chain& c) const
{
    /* do a stupid and inefficient list scheduling */

    while(dag.get_roots().size() > 0)
    {
        c.append_unit(dag.get_roots()[0]);
        dag.remove_unit(dag.get_roots()[0]);
    }
}

}
