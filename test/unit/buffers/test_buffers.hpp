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

#include <boost/capy/buffers/copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/buffers/slice.hpp>
#include <boost/core/detail/string_view.hpp>
#include <boost/assert.hpp>
#include <boost/static_assert.hpp>
#include <string>

// Trick boostdep into requiring URL
// since we need it for the unit tests
#ifdef BOOST_RUNTIME_SERVICES_BOOSTDEP
#include <boost/url/url.hpp>
#endif

#include "test_suite.hpp"

namespace boost {
namespace capy {

namespace test {

//------------------------------------------------

// these handle the case where n > size()

inline
core::string_view
trimmed_front(
    core::string_view s,
    std::size_t n) noexcept
{
    if(n > s.size())
        return {};
    return { s.data() + n, s.size() - n };
}

inline
core::string_view
trimmed_back(
    core::string_view s,
    std::size_t n) noexcept
{
    if(n > s.size())
        return {};
    return { s.data(), s.size() - n };
}

inline
core::string_view
kept_front(
    core::string_view s,
    std::size_t n) noexcept
{
    if(n >= s.size())
        return s;
    return { s.data(), n };
}

inline
core::string_view
kept_back(
    core::string_view s,
    std::size_t n) noexcept
{
    if(n >= s.size())
        return s;
    return { s.data() + s.size() - n, n };
}

// Return a string representing the buffer sequence
template<class ConstBufferSequence>
core::string_view
make_string(
    ConstBufferSequence const& bs)
{
    static char tmp[128];
    auto const n = copy(
        mutable_buffer(tmp, sizeof(tmp)), bs);
    return { tmp, n };
}

//------------------------------------------------

// Check the behavior of iterators
template<ConstBufferSequence CB>
void
check_iterators(
    CB bs,
    core::string_view pat,
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
    core::string_view pat)
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
    core::string_view pat0,
    bool deep)
{
    std::string tmp;

    for(std::size_t n = 0; n <= pat0.size() + 1; ++n)
    {
        {
            auto pat = trimmed_front(pat0, n);
            slice_type<ConstBufferSequence> bs(bs0);
            remove_prefix(bs, n);
            check_eq(bs, pat);
            check_iterators(bs, pat, tmp);

            if(deep)
            {
                // Take a copy, blank out the original to invalidate any
                // iterators, and redo the test
                slice_type<ConstBufferSequence> bsc(bs);
                {
                    slice_type<ConstBufferSequence> dummy{};
                    std::swap(bs, dummy);
                }
                for(std::size_t m = 0; m <= pat.size() + 1; ++m)
                {
                    auto pat2 = trimmed_front(pat, m);
                    slice_type<ConstBufferSequence> bs2(bsc);
                    remove_prefix(bs2, m);
                    check_eq(bs2, pat2);
                }
            }
        }
        {
            auto pat = kept_front(pat0, n);
            slice_type<ConstBufferSequence> bs(bs0);
            keep_prefix(bs, n);
            check_eq(bs, pat);
            check_iterators(bs, pat, tmp);
        }
    }
}

template<class ConstBufferSequence>
void
grind_back(
    ConstBufferSequence const& bs0,
    core::string_view pat0,
    bool deep)
{
    std::string tmp;

    for(std::size_t n = 0; n <= pat0.size() + 1; ++n)
    {
        {
            auto pat = trimmed_back(pat0, n);
            slice_type<ConstBufferSequence> bs(bs0);
            remove_suffix(bs, n);
            check_eq(bs, pat);
            check_iterators(bs, pat, tmp);
            if(deep)
            {
                // Take a copy, blank out the original to invalidate any
                // iterators, and redo the test
                slice_type<ConstBufferSequence> bsc(bs);
                {
                    slice_type<ConstBufferSequence> dummy{};
                    std::swap(bs, dummy);
                }
                for(std::size_t m = 0; m <= pat.size() + 1; ++m)
                {
                    auto pat2 = trimmed_back(pat, m);
                    slice_type<ConstBufferSequence> bs2(bsc);
                    remove_suffix(bs2, m);
                    check_eq(bs2, pat2);
                }
            }
        }
        {
            auto pat = kept_back(pat0, n);
            slice_type<ConstBufferSequence> bs(bs0);
            keep_suffix(bs, n);
            check_eq(bs, pat);
            check_iterators(bs, pat, tmp);
        }
    }
}

template<class ConstBufferSequence>
void
check_slice(
    ConstBufferSequence const& bs,
    core::string_view pat,
    bool deep)
{
    grind_front(bs, pat, deep);
    grind_back(bs, pat, deep);
}

// Test API and behavior of a BufferSequence
template<ConstBufferSequence CB>
void
check_sequence(
    CB const& t, core::string_view pat, bool deep = false)
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
