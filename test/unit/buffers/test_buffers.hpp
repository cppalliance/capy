//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_TEST_BUFFERS_HPP
#define BOOST_CAPY_BUFFERS_TEST_BUFFERS_HPP

#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/buffer_slice.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <string>
#include <string_view>

#include "test_suite.hpp"

namespace boost {
namespace capy {

namespace test {

//------------------------------------------------

// these handle the case where n > size()

inline
std::string_view
trimmed_front(
    std::string_view s,
    std::size_t n) noexcept
{
    if(n > s.size())
        return {};
    return { s.data() + n, s.size() - n };
}

inline
std::string_view
trimmed_back(
    std::string_view s,
    std::size_t n) noexcept
{
    if(n > s.size())
        return {};
    return { s.data(), s.size() - n };
}

inline
std::string_view
kept_front(
    std::string_view s,
    std::size_t n) noexcept
{
    if(n >= s.size())
        return s;
    return { s.data(), n };
}

inline
std::string_view
kept_back(
    std::string_view s,
    std::size_t n) noexcept
{
    if(n >= s.size())
        return s;
    return { s.data() + s.size() - n, n };
}

// Return a string representing the buffer sequence
template<class ConstBufferSequence>
std::string_view
make_string(
    ConstBufferSequence const& bs)
{
    static char tmp[128];
    auto const n = buffer_copy(
        mutable_buffer(tmp, sizeof(tmp)), bs);
    return { tmp, n };
}

//------------------------------------------------

// Check the behavior of iterators
template<ConstBufferSequence CB>
void
check_iterators(
    CB bs,
    std::string_view pat,
    std::string& s)
{
    BOOST_TEST_EQ(buffer_size(bs), pat.size());

    auto const& ct = bs;

    //std::string s;
    s.reserve(pat.size() + 1);

    // operator++()
    {
        s.clear();
        s.reserve(pat.size() + 1);
        auto it = begin(bs);
        auto const end_ = end(bs);
        while(it != end_)
        {
            auto b = *it;
            s.append(static_cast<
                char const*>(b.data()),
                b.size());
            ++it;
        }
        BOOST_TEST_EQ(s, pat);
    }

    // operator++(int)
    {
        s.clear();
        s.reserve(pat.size() + 1);
        auto it = begin(bs);
        auto const end_ = end(bs);
        while(it != end_)
        {
            auto b = *it;
            s.append(static_cast<
                char const*>(b.data()),
                b.size());
            it++;
        }
        BOOST_TEST_EQ(s, pat);
    }

    // operator++() const
    {
        s.clear();
        s.reserve(pat.size() + 1);
        auto it = begin(ct);
        auto const end_ = end(ct);
        while(it != end_)
        {
            auto b = *it;
            s.append(static_cast<
                char const*>(b.data()),
                b.size());
            ++it;
        }
        BOOST_TEST_EQ(s, pat);
    }

    // operator++(int) const
    {
        s.clear();
        s.reserve(pat.size() + 1);
        auto it = begin(ct);
        auto const end_ = end(ct);
        while(it != end_)
        {
            auto b = *it;
            s.append(static_cast<
                char const*>(b.data()),
                b.size());
            it++;
        }
        BOOST_TEST_EQ(s, pat);
    }

    // operator--()
    {
        s.clear();
        s.reserve(pat.size() + 1);
        auto it = end(bs);
        auto const begin_ = begin(bs);
        while(it != begin_)
        {
            --it;
            auto b = *it;
            s.insert(0, static_cast<
                char const*>(b.data()),
                b.size());
        }
        BOOST_TEST_EQ(s, pat);
    }

    // operator--(int)
    {
        s.clear();
        s.reserve(pat.size() + 1);
        auto it = end(bs);
        auto const begin_ = begin(bs);
        while(it != begin_)
        {
            it--;
            auto b = *it;
            s.insert(0, static_cast<
                char const*>(b.data()),
                b.size());
        }
        BOOST_TEST_EQ(s, pat);
    }

    // operator--() const
    {
        s.clear();
        s.reserve(pat.size() + 1);
        auto it = end(ct);
        auto const begin_ = begin(ct);
        while(it != begin_)
        {
            --it;
            auto b = *it;
            s.insert(0, static_cast<
                char const*>(b.data()),
                b.size());
        }
        BOOST_TEST_EQ(s, pat);
    }

    // operator--(int) const
    {
        s.clear();
        s.reserve(pat.size() + 1);
        auto it = end(ct);
        auto const begin_ = begin(ct);
        while(it != begin_)
        {
            it--;
            auto b = *it;
            s.insert(0, static_cast<
                char const*>(b.data()),
                b.size());
        }
        BOOST_TEST_EQ(s, pat);
    }
}

// Check that a buffer sequence matches the pattern
template<class ConstBufferSequence>
void
check_eq(
    ConstBufferSequence const& bs,
    std::string_view pat)
{
    if(! BOOST_TEST_EQ(buffer_size(bs), pat.size()))
        return;
    auto const s = make_string(bs);
    if(! BOOST_TEST_EQ(s.size(), pat.size()))
        return;
    if(! BOOST_TEST_EQ(s, pat))
        return;
}

template<class ConstBufferSequence>
void
grind_front(
    ConstBufferSequence const& bs0,
    std::string_view pat0,
    bool deep)
{
    std::string tmp;
    std::size_t const total = buffer_size(bs0);

    for(std::size_t n = 0; n <= pat0.size() + 1; ++n)
    {
        {
            // remove_prefix: drop the first n bytes
            auto pat = trimmed_front(pat0, n);
            auto bs = buffer_slice(bs0);
            bs.remove_prefix(n);
            check_eq(bs.data(), pat);
            check_iterators(bs.data(), pat, tmp);

            if(deep)
            {
                // Take a copy, blank out the original, and redo the test
                auto bsc = bs;
                bs = decltype(bs){};
                for(std::size_t m = 0; m <= pat.size() + 1; ++m)
                {
                    auto pat2 = trimmed_front(pat, m);
                    auto bs2 = bsc;
                    bs2.remove_prefix(m);
                    check_eq(bs2.data(), pat2);
                }
            }
        }
        {
            // keep_prefix: keep only the first n bytes
            auto pat = kept_front(pat0, n);
            std::size_t const len = (n < total) ? n : total;
            auto bs = buffer_slice(bs0, 0, len);
            check_eq(bs.data(), pat);
            check_iterators(bs.data(), pat, tmp);
        }
    }
}

template<class ConstBufferSequence>
void
grind_back(
    ConstBufferSequence const& bs0,
    std::string_view pat0,
    bool deep)
{
    std::string tmp;
    std::size_t const total = buffer_size(bs0);

    for(std::size_t n = 0; n <= pat0.size() + 1; ++n)
    {
        {
            // remove_suffix: drop the last n bytes
            auto pat = trimmed_back(pat0, n);
            std::size_t const len = (n < total) ? total - n : 0;
            auto bs = buffer_slice(bs0, 0, len);
            check_eq(bs.data(), pat);
            check_iterators(bs.data(), pat, tmp);
            if(deep)
            {
                // Take a copy, blank out the original, and redo the test
                auto bsc = bs;
                bs = decltype(bs){};
                for(std::size_t m = 0; m <= pat.size() + 1; ++m)
                {
                    auto pat2 = trimmed_back(pat, m);
                    // Drop another m bytes from the back of bsc by
                    // length-capping a fresh slice of the same data.
                    std::size_t const len2 = buffer_size(bsc.data());
                    std::size_t const new_len =
                        (m < len2) ? len2 - m : 0;
                    auto bs2 = bsc;
                    // Walk forward (current state) and use remove_prefix
                    // to drop the front; for the back we need a fresh
                    // slice over the inner-window. Easiest: construct
                    // a new slice from the original at the right offset/length.
                    bs2 = buffer_slice(bs0, 0, new_len);
                    check_eq(bs2.data(), pat2);
                }
            }
        }
        {
            // keep_suffix: keep only the last n bytes
            auto pat = kept_back(pat0, n);
            std::size_t const offset = (n < total) ? total - n : 0;
            auto bs = buffer_slice(bs0, offset);
            check_eq(bs.data(), pat);
            check_iterators(bs.data(), pat, tmp);
        }
    }
}

template<class ConstBufferSequence>
void
check_slice(
    ConstBufferSequence const& bs,
    std::string_view pat,
    bool deep)
{
    grind_front(bs, pat, deep);
    grind_back(bs, pat, deep);
}

// Test API and behavior of a BufferSequence
template<ConstBufferSequence CB>
void
check_sequence(
    CB const& t, std::string_view pat, bool deep = false)
{

    std::string tmp;
    check_iterators(t, pat, tmp);
    check_slice(t, pat, deep);
}

} // test

inline
std::string const&
test_pattern()
{
    static std::string const pat =
        "012" "34567" "89abcde";
    return pat;
}

} // capy
} // boost

#endif
