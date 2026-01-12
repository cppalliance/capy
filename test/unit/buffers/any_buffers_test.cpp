//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/any_buffers.hpp>

#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/buffers/copy.hpp>
#include <boost/capy/buffers/slice.hpp>
#include <boost/capy/buffers/to_string.hpp>

#include <boost/core/detail/static_assert.hpp>
#include <boost/core/detail/string_view.hpp>

#include "test_buffers.hpp"
#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace buffers {

static_assert(  const_buffer_sequence<any_buffers<true>>);
static_assert(  const_buffer_sequence<any_buffers<false>>);
static_assert(! mutable_buffer_sequence<any_buffers<true>>);
static_assert(  mutable_buffer_sequence<any_buffers<false>>);

namespace {

} // (anon)

struct any_buffers_test
{
    template<class T>
    void checkConst(T const& t, core::string_view pat)
    {
        BOOST_TEST_EQ(to_string(t), pat);
        any_const_buffers ab(t);
        test::check_sequence(ab, pat);
        BOOST_TEST_EQ(length(ab), length(t));
        BOOST_TEST_EQ(to_string(ab), pat);
        any_const_buffers ab2(ab);
        BOOST_TEST_EQ(to_string(ab2), to_string(ab));
        BOOST_TEST_EQ(length(ab2), length(ab));
    }

    template<class T>
    void checkMutable(T const& t, core::string_view pat)
    {
        BOOST_TEST_EQ(size(t), pat.size());
        any_mutable_buffers ab(t);
        BOOST_TEST_EQ(length(ab), length(t));
        auto n = copy(ab, const_buffer(pat.data(), pat.size()));
        test::check_sequence(ab, pat);
        BOOST_TEST_EQ(n, pat.size());
        BOOST_TEST_EQ(to_string(ab), pat);
        BOOST_TEST_EQ(to_string(ab), to_string(t));
    }

    void testEmpty()
    {
        {
            any_const_buffers ab;
            BOOST_TEST(ab.is_small_buffers());
            BOOST_TEST(ab.is_small_iter());
            BOOST_TEST_EQ(size(ab), 0);
            BOOST_TEST_EQ(ab.begin(), ab.end());
            BOOST_TEST_EQ(length(ab), 0);
            auto it0 = ab.begin();
            auto it1 = it0;
            BOOST_TEST_EQ(it0, it1);
            BOOST_TEST_EQ(size(*it0), 0);
            ++it0;
            BOOST_TEST_EQ(size(*it0), 0);
            --it0;
            BOOST_TEST_EQ(size(*it0), 0);
            checkConst(ab, "");
        }
        {
            any_mutable_buffers ab;
            BOOST_TEST(ab.is_small_buffers());
            BOOST_TEST(ab.is_small_iter());
            BOOST_TEST_EQ(size(ab), 0);
            BOOST_TEST_EQ(ab.begin(), ab.end());
            BOOST_TEST_EQ(length(ab), 0);
            auto it0 = ab.begin();
            auto it1 = it0;
            BOOST_TEST_EQ(it0, it1);
            BOOST_TEST_EQ(size(*it0), 0);
            ++it0;
            BOOST_TEST_EQ(size(*it0), 0);
            --it0;
            BOOST_TEST_EQ(size(*it0), 0);
            checkMutable(ab, "");
            checkConst(ab, "");
        }
    }

    void run()
    {
        testEmpty();

        core::string_view s0 = "Hello, world!";
        core::string_view s1 = "Goodbye, wg21!";

        // const_buffer
        {
            const_buffer bs(s0.data(), s0.size());
            BOOST_TEST(any_const_buffers(bs).is_small_buffers());
            BOOST_TEST(any_const_buffers(bs).is_small_iter());
            checkConst(bs, s0);
        }

        // mutable_buffer
        {
            char buf[13];
            mutable_buffer bs(buf, sizeof(buf));
            BOOST_TEST(any_mutable_buffers().is_small_buffers());
            BOOST_TEST(any_mutable_buffers().is_small_iter());
            checkMutable(bs, s0);
            checkConst(bs, s0);
        }

        // const_buffer_pair
        {
            const_buffer_pair bs({{
                { s0.data(), s0.size() },
                { s1.data(), s1.size() } }});
            any_const_buffers ab(bs);
            BOOST_TEST(ab.is_small_buffers());
            BOOST_TEST(ab.is_small_iter());
            BOOST_TEST_EQ(to_string(ab), to_string(bs));
            BOOST_TEST_EQ(to_string(any_const_buffers(bs)), to_string(bs));
        }

        // mutable_buffer_pair
        {
            char b0[16];
            char b1[16];
            mutable_buffer_pair bs({{
                { b0, sizeof(b0) },
                { b1, sizeof(b1) } }});
            any_mutable_buffers ab(bs);
            BOOST_TEST(ab.is_small_buffers());
            BOOST_TEST(ab.is_small_iter());
            BOOST_TEST_EQ(size(ab), sizeof(b0) + sizeof(b1));
            BOOST_TEST_EQ(size(ab), size(bs));
        }

        // slice (big iterators)
        {
            const_buffer_pair bs({{
                { s0.data(), s0.size() },
                { s1.data(), s1.size() } }});
            slice_of<const_buffer_pair> cb(bs);
            any_const_buffers ab(cb);
            BOOST_TEST(! ab.is_small_buffers());
            BOOST_TEST(! ab.is_small_iter());
            BOOST_TEST_EQ(to_string(ab), to_string(bs));
            BOOST_TEST_EQ(to_string(any_const_buffers(bs)), to_string(bs));
        }
    }
};

TEST_SUITE(
    any_buffers_test,
    "boost.capy.buffers.any_buffers");

} // buffers
} // capy
} // boost
