//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_write_stream.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/write_stream.hpp>

#include "test/unit/test_helpers.hpp"

#include <array>
#include <string_view>

namespace boost {
namespace capy {
namespace {

class any_write_stream_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_write_stream aws;
            BOOST_TEST(!aws.has_value());
            BOOST_TEST(!aws);
        }

        // Construct from stream
        {
            test::fuse f;
            test::write_stream ws(f);
            any_write_stream aws(ws);
            BOOST_TEST(aws.has_value());
            BOOST_TEST(static_cast<bool>(aws));
        }
    }

    void
    testMove()
    {
        test::fuse f;
        test::write_stream ws(f);

        any_write_stream aws1(ws);
        BOOST_TEST(aws1.has_value());

        // Move construct
        any_write_stream aws2(std::move(aws1));
        BOOST_TEST(aws2.has_value());
        BOOST_TEST(!aws1.has_value());

        // Move assign
        any_write_stream aws3;
        aws3 = std::move(aws2);
        BOOST_TEST(aws3.has_value());
        BOOST_TEST(!aws2.has_value());
    }

    void
    testWriteSome()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(ws);

            char const data[] = "hello world";
            const_buffer cb(data, 11);
            auto [ec, n] = co_await aws.write_some(std::span(&cb, 1));
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomePartial()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f, 5); // max 5 bytes per write

            any_write_stream aws(ws);

            char const data[] = "hello world";
            const_buffer cb(data, 11);
            auto [ec, n] = co_await aws.write_some(std::span(&cb, 1));
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(ws.data(), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeMultiple()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(ws);

            char const data1[] = "hello";
            const_buffer cb1(data1, 5);
            auto [ec1, n1] = co_await aws.write_some(std::span(&cb1, 1));
            if(ec1.failed())
                co_return;
            BOOST_TEST_EQ(n1, 5u);

            char const data2[] = " ";
            const_buffer cb2(data2, 1);
            auto [ec2, n2] = co_await aws.write_some(std::span(&cb2, 1));
            if(ec2.failed())
                co_return;
            BOOST_TEST_EQ(n2, 1u);

            char const data3[] = "world";
            const_buffer cb3(data3, 5);
            auto [ec3, n3] = co_await aws.write_some(std::span(&cb3, 1));
            if(ec3.failed())
                co_return;
            BOOST_TEST_EQ(n3, 5u);

            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeBufferSequence()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(ws);

            char const data1[] = "hello";
            char const data2[] = "world";
            std::array<const_buffer, 2> buffers = {{
                const_buffer(data1, 5),
                const_buffer(data2, 5)
            }};

            auto [ec, n] = co_await aws.write_some(
                std::span<const_buffer const>(buffers));
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeSingleBuffer()
    {
        // Single buffer passed directly (not wrapped in span)
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(ws);

            char const data[] = "hello world";
            const_buffer cb(data, 11);
            auto [ec, n] = co_await aws.write_some(cb);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeArray()
    {
        // Array of buffers passed directly (not converted to span)
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_stream ws(f);

            any_write_stream aws(ws);

            char const data1[] = "hello";
            char const data2[] = "world";
            std::array<const_buffer, 2> buffers = {{
                const_buffer(data1, 5),
                const_buffer(data2, 5)
            }};

            auto [ec, n] = co_await aws.write_some(buffers);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testMove();
        testWriteSome();
        testWriteSomePartial();
        testWriteSomeMultiple();
        testWriteSomeBufferSequence();
        testWriteSomeSingleBuffer();
        testWriteSomeArray();
    }
};

TEST_SUITE(any_write_stream_test, "boost.capy.io.any_write_stream");

} // namespace
} // namespace capy
} // namespace boost
