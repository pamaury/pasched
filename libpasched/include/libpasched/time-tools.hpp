#ifndef __PAMAURY_TIME_TOOLS_HPP__
#define __PAMAURY_TIME_TOOLS_HPP__

#include "config.hpp"
#include <string>
#include <vector>

namespace PAMAURY_SCHEDULER_NS
{

class timer
{
    public:
    timer();
    ~timer();

    void start();
    void stop();
    void reset();

    typedef unsigned long time_stat_t;

    /* get_value is updated only when stop or reset is called */
    time_stat_t get_value() const;
    time_stat_t get_hz() const;

    protected:
    void *m_opaque;
};

class time_stat
{
    public:
    time_stat(const std::string& name, bool do_register = true);
    ~time_stat();

    const std::string& get_name() const;
    timer& get_timer();
    const timer& get_timer() const;

    static void register_time_stat(time_stat *ts);
    static void unregister_time_stat(time_stat *ts);
    static size_t get_time_stat_count();
    static time_stat *get_time_stat_by_index(size_t idx);
    static time_stat *get_time_stat_by_name(const std::string& str);

    protected:
    std::string m_name;
    timer m_timer;
    bool m_registered;
};

}

#endif /* __PAMAURY_TIME_TOOLS_HPP__ */
