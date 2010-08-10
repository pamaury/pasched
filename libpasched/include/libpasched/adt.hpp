#ifndef __PAMAURY_ADT_HPP__
#define __PAMAURY_ADT_HPP__

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <cassert>
#include <string.h> /* for memset */

namespace PAMAURY_SCHEDULER_NS
{

namespace
{

inline size_t popcount(unsigned int x)
{
    return __builtin_popcount(x);
}

inline size_t popcount(unsigned long x)
{
    return __builtin_popcountl(x);
}

inline size_t popcount(unsigned long long x)
{
    return __builtin_popcountll(x);
}

inline size_t clz(unsigned int x)
{
    return __builtin_clz(x);
}

inline size_t clz(unsigned long x)
{
    return __builtin_clzl(x);
}

inline size_t clz(unsigned long long x)
{
    return __builtin_clzll(x);
}

inline size_t ctz(unsigned int x)
{
    return __builtin_ctz(x);
}

inline size_t ctz(unsigned long x)
{
    return __builtin_ctzl(x);
}

inline size_t ctz(unsigned long long x)
{
    return __builtin_ctzll(x);
}

template< typename T >
inline size_t find_first_bit_set(T a)
{
    return ctz(a);
}

template< typename T >
inline size_t find_last_bit_set(T a)
{
    return sizeof(a) * 8 - 1 - clz(a);
}

template< typename T >
inline bool test_bit(T a, size_t cur_pos)
{
    return (a >> cur_pos) & 0x1;
}

template< typename T >
inline size_t find_next_bit_set(T a, size_t cur_pos)
{
    while(!test_bit(a, cur_pos))
        cur_pos++;
    return cur_pos;
}

} /* namespace */

template< size_t MAX_CHUNKS >
struct bitmap_N_const_bit_set_iterator;

template< size_t MAX_CHUNKS >
struct bitmap_N
{
    typedef bitmap_N_const_bit_set_iterator< MAX_CHUNKS > const_bit_set_iterator;
    
    bitmap_N()
    {
        set_nb_bits(0);
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

    bool test_bit(size_t b) const
    {
        return pasched::test_bit(m_chunks[b / BITS_PER_CHUNKS ], b % BITS_PER_CHUNKS);
    }

    template< size_t N >
    bitmap_N< MAX_CHUNKS >& operator|=(const bitmap_N< N >& o)
    {
        assert(m_nb_bits == o.m_nb_bits);
        for(size_t i = 0; i < m_nb_chunks; i++)
            m_chunks[i] |= o.m_chunks[i];
        return *this;
    }

    void clear()
    {
        memset(m_chunks, 0, sizeof m_chunks);
    }

    static size_t nbs(uintmax_t n)
    {
        return popcount(n);
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

    const_bit_set_iterator bit_set_begin() const
    {
        const_bit_set_iterator it(this, 0, 0);
        if(!test_bit(0))
            ++it;
        return it;
    }

    const_bit_set_iterator bit_set_end() const
    {
        return const_bit_set_iterator(this, m_nb_chunks, 0);
    }

    static const size_t BITS_PER_CHUNKS = sizeof(uintmax_t) * 8;
    uintmax_t m_chunks[MAX_CHUNKS];
    size_t m_nb_chunks;
    size_t m_nb_bits;
};

template< size_t MAX_CHUNKS >
struct bitmap_N_const_bit_set_iterator
{
    bitmap_N_const_bit_set_iterator()
        :m_bitmap(0) {}
    bitmap_N_const_bit_set_iterator(const bitmap_N< MAX_CHUNKS > *b, size_t c_idx, size_t b_idx)
        :m_bitmap(b), m_chunk_idx(c_idx), m_chunk_bit_idx(b_idx)
    {
        m_abs_bit_idx = c_idx * BITS_PER_CHUNKS + b_idx;
    }

    bitmap_N_const_bit_set_iterator& operator++()
    {
        /* special starting case */
        if(m_chunk_idx == 0 && m_chunk_bit_idx == 0 && m_bitmap->m_chunks[0] == 0)
            goto Ladv_chunk;
        /* advance in the chunk, if possible */
        m_chunk_bit_idx++;
        if(m_chunk_bit_idx < BITS_PER_CHUNKS)
        {
            /* we are still in the chunk, see if we are past the last bit set */
            if(m_chunk_bit_idx <= find_last_bit_set(m_bitmap->m_chunks[m_chunk_idx]))
            {
                /* find next bit set in the chunk */
                m_chunk_bit_idx = find_next_bit_set(m_bitmap->m_chunks[m_chunk_idx], m_chunk_bit_idx);
                goto Lend;
            }
            /* fallback */
        }
        Ladv_chunk:
        /* advance to next chunk */
        m_chunk_idx++;
        m_chunk_bit_idx = 0;
        while(m_chunk_idx < m_bitmap->m_nb_chunks && m_bitmap->m_chunks[m_chunk_idx] == 0)
            m_chunk_idx++;
        /* still there ? */
        if(m_chunk_idx < m_bitmap->m_nb_chunks)
            /* find first bit set */
            m_chunk_bit_idx = find_first_bit_set(m_bitmap->m_chunks[m_chunk_idx]);

        Lend:
        /* compute absolute position */
        m_abs_bit_idx = m_chunk_idx * BITS_PER_CHUNKS + m_chunk_bit_idx;

        return *this;
    }

    bool operator==(const bitmap_N_const_bit_set_iterator& o) const
    {
        return m_bitmap == o.m_bitmap && m_abs_bit_idx == o.m_abs_bit_idx;
    }

    bool operator!=(const bitmap_N_const_bit_set_iterator& o) const
    {
        return !operator==(o);
    }

    size_t operator*() const
    {
        return m_abs_bit_idx;
    }

    static const size_t BITS_PER_CHUNKS = bitmap_N< MAX_CHUNKS >::BITS_PER_CHUNKS;
    const bitmap_N< MAX_CHUNKS > *m_bitmap;
    size_t m_chunk_idx;
    size_t m_chunk_bit_idx;
    size_t m_abs_bit_idx;
};

typedef bitmap_N< 100 > bitmap; /* should be sufficient for all cases */

}

#endif /* __PAMAURY_ADT_HPP__ */
