#ifndef __PAMAURY_TOOLS_HPP__
#define __PAMAURY_TOOLS_HPP__

#include "config.hpp"
#include <vector>
#include <functional>
#include <string>

namespace PAMAURY_SCHEDULER_NS
{

template<typename T, typename U>
void unordered_vector_remove(U i, std::vector<T>& v)
{
    std::swap(v[i], v[v.size() - 1]);
    v.pop_back();
}

template<typename T>
void unordered_find_and_remove(const T& t, std::vector<T>& v, bool stop_on_first = false)
{
    for(int i = 0; i < (int)v.size(); ++i)
        if(v[i] == t)
        {
            unordered_vector_remove(i, v);
            i--;
            if(stop_on_first)
                break;
        }
}

template<typename U, typename T>
void unordered_find_and_remove(U uf, std::vector<T>& v, bool stop_on_first = false)
{
    for(int i = 0; i < (int)v.size(); ++i)
        if(uf(v[i]))
        {
            unordered_vector_remove(i, v);
            i--;
            if(stop_on_first)
                break;
        }
}

template<typename U, typename T>
bool container_contains(const U& cont, const T& t)
{
    typename U::const_iterator it = cont.begin();
    typename U::const_iterator it_end = cont.end();
    for(; it != it_end; ++it)
        if(*it == t)
            return true;
    return false;
}

std::string trim(const std::string& s);

}

#endif // __PAMAURY_TOOLS_HPP__
