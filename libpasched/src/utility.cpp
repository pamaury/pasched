#include "tools.hpp"

namespace PAMAURY_SCHEDULER_NS
{

std::string trim(const std::string& s)
{
    size_t f = s.find_first_not_of(" ");
    if(f == std::string::npos)
        return std::string();

    return s.substr(f, s.find_last_not_of(" ") - f + 1);
}

}
