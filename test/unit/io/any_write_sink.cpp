//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_write_sink.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/write_sink.hpp>

#include "test/unit/test_helpers.hpp"

#include <array>
#include <string_view>

namespace boost {
namespace capy {
namespace {

class any_write_sink_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_write_sink aws;
            BOOST_TEST(!aws.has_value());
            BOOST_TEST(!aws);
        }

        // Construct from sink
        {
            test::fuse f;
            test::write_sink ws(f);
            any_write_sink aws(&ws);
            BOOST_TEST(aws.has_value());
            BOOST_TEST(static_cast<bool>(aws));
        }
    }

    void
    testMove()
    {
        test::fuse f;
        test::write_sink ws(f);

        any_write_sink aws1(&ws);
        BOOST_TEST(aws1.has_value());

        // Move construct
        any_write_sink aws2(std::move(aws1));
        BOOST_TEST(aws2.has_value());
        BOOST_TEST(!aws1.has_value());

        // Move assign
        any_write_sink aws3;
        aws3 = std::move(aws2);
        BOOST_TEST(aws3.has_value());
        BOOST_TEST(!aws2.has_value());
    }

    void
    testWrite()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f, 5); // max 5 bytes per write

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write(
                make_buffer("hello world", 11));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST(!ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteMultiple()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec1, n1] = co_await aws.write(
                make_buffer("hello", 5));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 5u);

            auto [ec2, n2] = co_await aws.write(
                make_buffer(" ", 1));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 1u);

            auto [ec3, n3] = co_await aws.write(
                make_buffer("world", 5));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 5u);

            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteBufferSequence()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            std::array<const_buffer, 2> buffers = {{
                make_buffer("hello", 5),
                make_buffer("world", 5)
            }};

            auto [ec, n] = co_await aws.write(buffers);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSingleBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write(
                make_buffer("hello world", 11));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteWithEofFalse()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write(
                make_buffer("hello", 5), false);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(ws.data(), "hello");
            BOOST_TEST(!ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteWithEofTrue()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write(
                make_buffer("hello", 5), true);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(ws.data(), "hello");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteWithEofEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write(const_buffer(), true);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec] = co_await aws.write_eof();
            if(ec)
                co_return;

            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteThenWriteEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            auto [ec1, n] = co_await aws.write(
                make_buffer("hello", 5));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST(!ws.eof_called());

            auto [ec2] = co_await aws.write_eof();
            if(ec2)
                co_return;
            BOOST_TEST(ws.eof_called());
            BOOST_TEST_EQ(ws.data(), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteArray()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f);

            any_write_sink aws(&ws);

            std::array<const_buffer, 2> buffers = {{
                make_buffer("hello", 5),
                make_buffer("world", 5)
            }};

            auto [ec, n] = co_await aws.write(buffers);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    testWritePartial()
    {
        // Verify that any_write_sink loops to consume all data
        // even when underlying sink has max_write_size
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f, 5); // max 5 bytes per write

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write(
                make_buffer("hello world", 11));
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteWithEofPartial()
    {
        // Verify that any_write_sink loops to consume all data
        // and signals eof even when underlying sink has max_write_size
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::write_sink ws(f, 5); // max 5 bytes per write

            any_write_sink aws(&ws);

            auto [ec, n] = co_await aws.write(
                make_buffer("hello world", 11), true);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testMove();
        testWrite();
        testWriteMultiple();
        testWriteBufferSequence();
        testWriteSingleBuffer();
        testWriteWithEofFalse();
        testWriteWithEofTrue();
        testWriteWithEofEmpty();
        testWriteEof();
        testWriteThenWriteEof();
        testWriteArray();
        testWritePartial();
        testWriteWithEofPartial();
    }
};

TEST_SUITE(any_write_sink_test, "boost.capy.io.any_write_sink");

} // namespace
} // namespace capy
} // namespace boost
