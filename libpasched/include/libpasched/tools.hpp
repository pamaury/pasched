#ifndef __PAMAURY_TOOLS_HPP__
#define __PAMAURY_TOOLS_HPP__

#include "config.hpp"
#include <vector>
#include <functional>
#include <string>
#include <set>
#include <iostream>

namespace PAMAURY_SCHEDULER_NS
{

template<typename T>
std::ostream& operator<<(std::ostream& os, const std::set< T >& s)
{
    typename std::set< T >::const_iterator it = s.begin();
    os << "{";

    while(it != s.end())
    {
        os << *it;
        ++it;
        if(it == s.end())
            break;
        os << ", ";
    }
    
    return os << "}";
}

template<typename T>
std::ostream& operator<<(std::ostream& os, const std::vector< T >& v)
{
    os << "[";

    for(size_t i = 0; i < v.size(); i++)
    {
        os << v[i];
        if((i + 1) != v.size())
            os << ", ";
    }
    
    return os << "]";
}

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

template<typename T>
void unordered_find_and_modify(const T& t, const T& mod, std::vector<T>& v, bool stop_on_first = false)
{
    for(size_t i = 0; i < v.size(); ++i)
        if(v[i] == t)
        {
            v[i] = mod;
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

template<typename T>
std::set< T > set_minus(const std::set< T >& a, const std::set< T >& b)
{
    std::set< T > c = a;
    typename std::set< T >::const_iterator it = b.begin(), it_end = b.end();
    while(it != it_end)
    {
        c.erase(*it);
        ++it;
    }
    return c;
}

template<typename T>
std::set< T > set_union(const std::set< T >& a, const std::set< T >& b)
{
    std::set< T > c = a;
    c.insert(b.begin(), b.end());
    return c;
}

template<typename T>
std::set< T > set_inter(const std::set< T >& a, const std::set< T >& b)
{
    std::set< T > c;
    for(typename std::set< T >::const_iterator it = a.begin(); it != a.end(); ++it)
        if(b.find(*it) != b.end())
            c.insert(*it);
    return c;
}

template<typename T>
std::vector< T > set_to_vector(const std::set< T >& s)
{
    std::vector< T > v;
    v.insert(v.begin(), s.begin(), s.end());
    return v;
}

std::string trim(const std::string& s);

std::ostream& debug();

void set_debug(std::ostream& s);

}

#endif // __PAMAURY_TOOLS_HPP__
