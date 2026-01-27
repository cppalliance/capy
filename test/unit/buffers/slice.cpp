//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/slice.hpp>

#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>

#include <array>
#include <string_view>
#include <vector>

#include "test_buffers.hpp"
#include "test_suite.hpp"

namespace boost {
namespace capy {

template<
    std::size_t I,
    std::size_t N>
void
set(
    std::string&,
    std::array<const_buffer, N>&)
{
}

template<
    std::size_t I,
    std::size_t N,
    class... Args>
void
set(
    std::string& s,
    std::array<const_buffer, N>& v,
    char const* p,
    Args const&... args)
{
    std::string_view sv(p);
    v[I] = make_buffer(sv);
    s.append(sv.data(), sv.size());
    set<I+1>(s, v, args...);
}

auto
make_buffers(
    std::string&) ->
    std::array<const_buffer, 0>
{
    return {};
}

template<
    class... Args>
auto
make_buffers(
    std::string& s,
    char const* arg0,
    Args const&... args) ->
    std::array<const_buffer, 1 + sizeof...(Args)>
{
    s = {};
    std::array<const_buffer, 1 + sizeof...(Args)> v;
    set<0>(s, v, arg0, args...);
    return v;
}

struct slice_test
{
    static
    void
    checkStatic()
    {
        using T = slice_of<const_buffer_pair>;

        static_assert(std::is_default_constructible<T>::value);
        static_assert(std::is_copy_constructible<T>::value);
        static_assert(std::is_move_constructible<T>::value);
        static_assert(std::is_copy_assignable<T>::value);
        static_assert(std::is_move_assignable<T>::value);

        using U = T::const_iterator;

        static_assert(std::is_default_constructible<U>::value);
        static_assert(std::is_copy_constructible<U>::value);
        static_assert(std::is_move_constructible<U>::value);
        static_assert(std::is_copy_assignable<U>::value);
        static_assert(std::is_move_assignable<U>::value);
    }

    template<class B>
    static
    void
    check(
        B const& b,
        std::string_view s)
    {
        auto constexpr M = 1024;
        char buf[M];
        if(! BOOST_TEST_LE(buffer_size(b), M))
            return;
        if(! BOOST_TEST_EQ(buffer_size(b), s.size()))
            return;
        auto const n = buffer_copy(
            mutable_buffer(buf, M), b);
        if(! BOOST_TEST_EQ(n, s.size()))
            return;
        if(! BOOST_TEST_EQ(std::string_view(buf, n), s))
            return;

        std::string tmp;
        test::check_iterators(b, s, tmp);
    }

    // Use a vector so that iterator invalidation is observable during testing.
    using seq_type = std::vector<const_buffer>;

    void
    grind_back(
        slice_of<seq_type> const& bs0,
        std::string_view pat0)
    {
        auto const n = buffer_size(bs0);
        if(! BOOST_TEST_EQ(n, pat0.size()))
            return;
        for(std::size_t i = 0; i < n; ++i)
        {
            auto bs = bs0;
            auto pat = pat0.substr(0, pat0.size() - i);
            remove_suffix(bs, i);
            check(bs, pat);
        }
        // n >= buffer_size
        for(std::size_t i = 0; i < 2; ++i)
        {
            auto bs = bs0;
            remove_suffix(bs, n + i);
            BOOST_TEST_EQ(buffer_size(bs), 0);
            check(bs, "");
        }
    }

    void
    grind(
        slice_of<seq_type> const& bs0,
        std::string_view pat0)
    {
        auto const n = buffer_size(bs0);
        if(! BOOST_TEST_EQ(n, pat0.size()))
            return;
        for(std::size_t i = 0; i < n; ++i)
        {
            auto bs = bs0;
            auto pat = pat0.substr(i);
            remove_prefix(bs, i);
            check(bs, pat);
            grind_back(bs, pat);
        }
        // n >= buffer_size
        for(std::size_t i = 0; i < 2; ++i)
        {
            auto bs = bs0;
            remove_prefix(bs, n + i);
            BOOST_TEST_EQ(buffer_size(bs), 0);
            check(bs, "");
        }
    }

    void
    testSansPrefixSingleBuffer()
    {
        // Test sans_prefix with a single mutable_buffer
        {
            char data[] = "0123456789";
            mutable_buffer buf(data, 10);

            // sans_prefix(buf, 0) should return the full buffer
            auto s0 = sans_prefix(buf, 0);
            BOOST_TEST_EQ(buffer_size(s0), 10u);

            // sans_prefix(buf, 3) should skip first 3 bytes
            auto s3 = sans_prefix(buf, 3);
            BOOST_TEST_EQ(buffer_size(s3), 7u);
            BOOST_TEST_EQ(
                static_cast<char const*>(
                    const_buffer(s3).data())[0], '3');

            // sans_prefix(buf, 10) should be empty
            auto s10 = sans_prefix(buf, 10);
            BOOST_TEST_EQ(buffer_size(s10), 0u);

            // sans_prefix(buf, 100) should be empty
            auto s100 = sans_prefix(buf, 100);
            BOOST_TEST_EQ(buffer_size(s100), 0u);
        }

        // Test sans_prefix with a single const_buffer
        {
            char data[] = "Hello World";
            const_buffer buf(data, 11);

            auto s0 = sans_prefix(buf, 0);
            BOOST_TEST_EQ(buffer_size(s0), 11u);

            auto s6 = sans_prefix(buf, 6);
            BOOST_TEST_EQ(buffer_size(s6), 5u);
            BOOST_TEST_EQ(
                static_cast<char const*>(s6.data())[0], 'W');
        }
    }

    void
    testSansPrefixBufferSequence()
    {
        // Test sans_prefix with a vector of buffers
        std::string s1 = "ABCD";
        std::string s2 = "EFGH";
        std::string s3 = "IJKL";

        std::vector<const_buffer> bufs = {
            const_buffer(s1.data(), s1.size()),
            const_buffer(s2.data(), s2.size()),
            const_buffer(s3.data(), s3.size())
        };

        // sans_prefix removing nothing
        {
            auto result = sans_prefix(bufs, 0);
            BOOST_TEST_EQ(buffer_size(result), 12u);
        }

        // sans_prefix removing 2 bytes (within first buffer)
        {
            auto result = sans_prefix(bufs, 2);
            BOOST_TEST_EQ(buffer_size(result), 10u);
        }

        // sans_prefix removing 5 bytes (crosses buffer boundary)
        {
            auto result = sans_prefix(bufs, 5);
            BOOST_TEST_EQ(buffer_size(result), 7u);
        }

        // sans_prefix removing all
        {
            auto result = sans_prefix(bufs, 12);
            BOOST_TEST_EQ(buffer_size(result), 0u);
        }
    }

    void
    testBufferEmptyWithSlice()
    {
        // Verify buffer_empty works correctly with sliced buffers
        {
            char data[] = "test";
            mutable_buffer buf(data, 4);

            auto s0 = sans_prefix(buf, 0);
            BOOST_TEST(!buffer_empty(s0));

            auto s4 = sans_prefix(buf, 4);
            BOOST_TEST(buffer_empty(s4));
        }
    }

    void
    testSansPrefixLoop()
    {
        // Test the pattern used in any_buffer_source::read()
        char data[10] = {};
        mutable_buffer buf(data, 10);

        auto dest = sans_prefix(buf, 0);
        BOOST_TEST_EQ(buffer_size(dest), 10u);
        BOOST_TEST(!buffer_empty(dest));

        // Simulate consuming 2 bytes
        dest = sans_prefix(dest, 2);
        BOOST_TEST_EQ(buffer_size(dest), 8u);
        BOOST_TEST(!buffer_empty(dest));

        // Consume remaining
        dest = sans_prefix(dest, 8);
        BOOST_TEST_EQ(buffer_size(dest), 0u);
        BOOST_TEST(buffer_empty(dest));
    }

    void
    run()
    {
        std::string s;
        auto a = make_buffers(s, "boost.", "buffers.", "slice_");
        seq_type bs(a.begin(), a.end());
        test::check_sequence(bs, s, true);
        //check(bs, s);
        //grind(bs, s);

        testSansPrefixSingleBuffer();
        testSansPrefixBufferSequence();
        testBufferEmptyWithSlice();
        testSansPrefixLoop();
    }
};

TEST_SUITE(
    slice_test,
    "boost.capy.buffers.slice");

} // capy
} // boost
