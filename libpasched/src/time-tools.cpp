#include "time-tools.hpp"
#include "tools.hpp"
#include <ctime>
#include <stdexcept>

namespace PAMAURY_SCHEDULER_NS
{

/**
 * timer
 */

struct timer_data
{
    clock_t value;
    bool running;
    clock_t start;
};

#define td  (*(timer_data *)m_opaque)
#define ctd  (*(const timer_data *)m_opaque)

timer::timer()
{
    m_opaque = new timer_data;
    td.value = 0;
    td.running = false;
    td.start = 0;
}

timer::~timer()
{
    delete (timer_data *)m_opaque;
}

void timer::start()
{
    td.running = true;
    td.start = clock();
}

void timer::stop()
{
    td.value += clock() - td.start;
    td.running = false;
}

void timer::reset()
{
    td.running = false;
    td.value = 0;
}

timer::time_stat_t timer::get_value() const
{
    return ctd.value;
}

timer::time_stat_t timer::get_hz() const
{
    return CLOCKS_PER_SEC;
}

/**
 * time_stat
 */
time_stat::time_stat(const std::string& name, bool do_register)
    :m_name(name), m_registered(do_register)
{
    if(m_registered)
        time_stat::register_time_stat(this);
}

time_stat::~time_stat()
{
    if(m_registered)
        time_stat::unregister_time_stat(this);
}

const std::string& time_stat::get_name() const
{
    return m_name;
}

const timer& time_stat::get_timer() const
{
    return m_timer;
}

timer& time_stat::get_timer()
{
    return m_timer;
}

namespace
{
    /* avoid "static initialization order fiasco" */
    std::vector< time_stat * > *__g_time_stats = 0;
    std::vector< time_stat * >& g_time_stats()
    {
        if(__g_time_stats == 0)
            __g_time_stats = new std::vector< time_stat * >;
        return *__g_time_stats;
    }
}

void time_stat::register_time_stat(time_stat *ts)
{
    g_time_stats().push_back(ts);
}

void time_stat::unregister_time_stat(time_stat *ts)
{
    unordered_find_and_remove(ts, g_time_stats());
}

size_t time_stat::get_time_stat_count()
{
    return g_time_stats().size();
}

time_stat *time_stat::get_time_stat_by_index(size_t idx)
{
    return g_time_stats()[idx];
}

time_stat *time_stat::get_time_stat_by_name(const std::string& str)
{
    for(size_t i = 0; i < g_time_stats().size(); i++)
        if(str == g_time_stats()[i]->get_name())
            return g_time_stats()[i];
    throw std::runtime_error("time_stat::get_time_stat_by_name can't find name in registered time stats");
}

}
