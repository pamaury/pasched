#include "tools.hpp"
#include <iostream>

namespace PAMAURY_SCHEDULER_NS
{


class nullstream : public std::ostream
{
    public:
    nullstream(): std::ios(0), std::ostream(0) {}
};

nullstream null;
std::ostream *g_debug = &null;

std::ostream& debug()
{
    return *g_debug;
}

void set_debug(std::ostream& s)
{
    g_debug = &s;
}

std::string trim(const std::string& s)
{
    size_t f = s.find_first_not_of(" ");
    if(f == std::string::npos)
        return std::string();

    return s.substr(f, s.find_last_not_of(" ") - f + 1);
}

}
