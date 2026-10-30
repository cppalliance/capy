//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that the header is self-contained.
#include <boost/capy/buffers/buffers_front.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <vector>

#include "test_suite.hpp"

namespace boost {
namespace capy {

namespace {

struct user_mutable
{
    char* p;
    std::size_t n;

    operator mutable_buffer() const noexcept { return { p, n }; }
    operator const_buffer() const noexcept { return { p, n }; }
};

struct user_const
{
    char const* p;
    std::size_t n;

    operator const_buffer() const noexcept { return { p, n }; }
};

struct buffers_front_test
{
    void
    testSingleBuffer()
    {
        char data[8] = {};

        mutable_buffer mb(data, 8);
        auto m = buffers_front(mb);
        static_assert(std::same_as<decltype(m), mutable_buffer>);
        BOOST_TEST_EQ(m.data(), data);
        BOOST_TEST_EQ(m.size(), 8);

        const_buffer cb(data, 3);
        auto c = buffers_front(cb);
        static_assert(std::same_as<decltype(c), const_buffer>);
        BOOST_TEST_EQ(c.data(), data);
        BOOST_TEST_EQ(c.size(), 3);
    }

    void
    testRange()
    {
        char a[4] = {};
        char b[6] = {};

        std::array<mutable_buffer, 2> mbs{{
            mutable_buffer(a, 4), mutable_buffer(b, 6) }};
        auto m = buffers_front(mbs);
        static_assert(std::same_as<decltype(m), mutable_buffer>);
        BOOST_TEST_EQ(m.data(), a);
        BOOST_TEST_EQ(m.size(), 4);

        std::array<const_buffer, 2> cbs{{
            const_buffer(b, 6), const_buffer(a, 4) }};
        auto c = buffers_front(cbs);
        static_assert(std::same_as<decltype(c), const_buffer>);
        BOOST_TEST_EQ(c.data(), b);
        BOOST_TEST_EQ(c.size(), 6);
    }

    void
    testEmptyRange()
    {
        std::vector<mutable_buffer> v;
        auto m = buffers_front(v);
        BOOST_TEST_EQ(m.data(), nullptr);
        BOOST_TEST_EQ(m.size(), 0);

        std::vector<const_buffer> cv;
        auto c = buffers_front(cv);
        BOOST_TEST_EQ(c.data(), nullptr);
        BOOST_TEST_EQ(c.size(), 0);
    }

    void
    testUserTypes()
    {
        char a[4] = {};
        char b[6] = {};

        user_mutable um{ a, 4 };
        auto m = buffers_front(um);
        static_assert(std::same_as<decltype(m), mutable_buffer>);
        BOOST_TEST_EQ(m.data(), a);
        BOOST_TEST_EQ(m.size(), 4);

        std::array<user_mutable, 2> ums{{ { b, 6 }, { a, 4 } }};
        auto ma = buffers_front(ums);
        static_assert(std::same_as<decltype(ma), mutable_buffer>);
        BOOST_TEST_EQ(ma.data(), b);
        BOOST_TEST_EQ(ma.size(), 6);

        user_const uc{ a, 4 };
        auto c = buffers_front(uc);
        static_assert(std::same_as<decltype(c), const_buffer>);
        BOOST_TEST_EQ(c.data(), a);
        BOOST_TEST_EQ(c.size(), 4);

        std::array<user_const, 2> ucs{{ { b, 6 }, { a, 4 } }};
        auto ca = buffers_front(ucs);
        static_assert(std::same_as<decltype(ca), const_buffer>);
        BOOST_TEST_EQ(ca.data(), b);
        BOOST_TEST_EQ(ca.size(), 6);
    }

    void
    run()
    {
        testSingleBuffer();
        testRange();
        testEmptyRange();
        testUserTypes();
    }
};

TEST_SUITE(buffers_front_test, "boost.capy.buffers.buffers_front");

} // (anon)

} // namespace capy
} // namespace boost
