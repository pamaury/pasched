#include "scheduler.hpp"

namespace PAMAURY_SCHEDULER_NS
{

scheduler::scheduler(const schedule_dag& sd)
    :m_graph(sd)
{
}

scheduler::~scheduler()
{
}

}
