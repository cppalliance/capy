//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_read_stream.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/read_stream.hpp>

#include "test/unit/test_helpers.hpp"

#include <array>
#include <string_view>

namespace boost {
namespace capy {
namespace {

class any_read_stream_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_read_stream ars;
            BOOST_TEST(!ars.has_value());
            BOOST_TEST(!ars);
        }

        // Construct from stream
        {
            test::fuse f;
            test::read_stream rs(f);
            any_read_stream ars(rs);
            BOOST_TEST(ars.has_value());
            BOOST_TEST(static_cast<bool>(ars));
        }
    }

    void
    testMove()
    {
        test::fuse f;
        test::read_stream rs(f);

        any_read_stream ars1(rs);
        BOOST_TEST(ars1.has_value());

        // Move construct
        any_read_stream ars2(std::move(ars1));
        BOOST_TEST(ars2.has_value());
        BOOST_TEST(!ars1.has_value());

        // Move assign
        any_read_stream ars3;
        ars3 = std::move(ars2);
        BOOST_TEST(ars3.has_value());
        BOOST_TEST(!ars2.has_value());
    }

    void
    testReadSome()
    {
        // Test with f.armed to exercise failure injection
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("hello world");

            any_read_stream ars(rs);

            char buf[32] = {};
            mutable_buffer mb(buf, sizeof(buf));
            auto [ec, n] = co_await ars.read_some(std::span(&mb, 1));
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomePartial()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("hello world");

            any_read_stream ars(rs);

            char buf[5] = {};
            mutable_buffer mb(buf, sizeof(buf));
            auto [ec, n] = co_await ars.read_some(std::span(&mb, 1));
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello");
            BOOST_TEST_EQ(rs.available(), 6u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeMultiple()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("abcdefghij");

            any_read_stream ars(rs);

            char buf[3] = {};
            mutable_buffer mb(buf, sizeof(buf));

            auto [ec1, n1] = co_await ars.read_some(std::span(&mb, 1));
            if(ec1.failed())
                co_return;
            BOOST_TEST_EQ(n1, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abc");

            auto [ec2, n2] = co_await ars.read_some(std::span(&mb, 1));
            if(ec2.failed())
                co_return;
            BOOST_TEST_EQ(n2, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "def");

            auto [ec3, n3] = co_await ars.read_some(std::span(&mb, 1));
            if(ec3.failed())
                co_return;
            BOOST_TEST_EQ(n3, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n3), "ghi");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            // No data provided - should get EOF

            any_read_stream ars(rs);

            char buf[32] = {};
            mutable_buffer mb(buf, sizeof(buf));
            auto [ec, n] = co_await ars.read_some(std::span(&mb, 1));
            if(ec.failed() && ec != cond::eof)
                co_return;

            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeBufferSequence()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("helloworld");

            any_read_stream ars(rs);

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                mutable_buffer(buf1, sizeof(buf1)),
                mutable_buffer(buf2, sizeof(buf2))
            }};

            auto [ec, n] = co_await ars.read_some(
                std::span<mutable_buffer const>(buffers));
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(buf2, 5), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testMove();
        testReadSome();
        testReadSomePartial();
        testReadSomeMultiple();
        testReadSomeEof();
        testReadSomeBufferSequence();
    }
};

TEST_SUITE(any_read_stream_test, "boost.capy.io.any_read_stream");

} // namespace
} // namespace capy
} // namespace boost
