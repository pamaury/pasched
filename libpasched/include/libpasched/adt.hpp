#ifndef __PAMAURY_ADT_HPP__
#define __PAMAURY_ADT_HPP__

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <cassert>
#include <string.h> /* for memset */

namespace PAMAURY_SCHEDULER_NS
{

template< size_t MAX_CHUNKS >
struct bitmap_N
{
    bitmap_N()
    {
    }
    
    bitmap_N(size_t nb_bits)
    {
        set_nb_bits(nb_bits);
    }

    void set_nb_bits(size_t nb_bits)
    {
        assert(nb_bits < BITS_PER_CHUNKS * MAX_CHUNKS && "bitmap is too small to handle such a number of bits");
        m_nb_bits = nb_bits;
        m_nb_chunks = (nb_bits + BITS_PER_CHUNKS - 1) / BITS_PER_CHUNKS;
        clear();
    }

    template< size_t N >
    bool operator<(const bitmap_N< N >& o) const
    {
        assert(o.m_nb_bits == m_nb_bits);

        for(int i = m_nb_chunks - 1; i >= 0; i--)
        {
            if(m_chunks[i] < o.m_chunks[i])
                return true;
            else if(m_chunks[i] > o.m_chunks[i])
                return false;
        }

        return false;
    }

    void set_bit(size_t b)
    {
        m_chunks[b / BITS_PER_CHUNKS] |= (uintmax_t)1 << (b % BITS_PER_CHUNKS);
    }

    void clear_bit(size_t b)
    {
        m_chunks[b / BITS_PER_CHUNKS] &= ~((uintmax_t)1 << (b % BITS_PER_CHUNKS));
    }

    void clear()
    {
        memset(m_chunks, 0, sizeof m_chunks);
    }

    static size_t nbs(uintmax_t n)
    {
        size_t cnt = 0;
        while(n != 0)
        {
            cnt += n % 2;
            n /= 2;
        }
        return cnt;
    }

    size_t nb_bits_set() const
    {
        size_t cnt = 0;
        for(size_t i = 0; i < m_nb_chunks; i++)
            cnt += nbs(m_chunks[i]);
        return cnt;
    }

    size_t nb_bits_cleared() const
    {
        return m_nb_bits - nb_bits_set();
    }

    template< size_t N >
    bool operator==(const bitmap_N< N >& o) const
    {
        assert(o.m_nb_bits == m_nb_bits);
        
        for(size_t i = 0; i < m_nb_chunks; i++)
            if(m_chunks[i] != o.m_chunks[i])
                return false;
        return true;
    }

    template< size_t N >
    bool operator!=(const bitmap_N< N >& o) const
    {
        return !operator==(o);
    }

    static const size_t BITS_PER_CHUNKS = sizeof(uintmax_t) * 8;
    uintmax_t m_chunks[MAX_CHUNKS];
    size_t m_nb_chunks;
    size_t m_nb_bits;
};

typedef bitmap_N< 100 > bitmap; /* should be sufficient for all cases */

}

#endif /* __PAMAURY_ADT_HPP__ */
