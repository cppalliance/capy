//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_buffer_source.hpp>

// Test that push_to header is self-contained.
#include <boost/capy/io/push_to.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/buffer_source.hpp>
#include <boost/capy/test/write_sink.hpp>

#include "test/unit/test_helpers.hpp"

#include <string_view>

namespace boost {
namespace capy {
namespace {

// Static assert that any_buffer_source satisfies BufferSource
static_assert(BufferSource<any_buffer_source>);

class any_buffer_source_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_buffer_source abs;
            BOOST_TEST(!abs.has_value());
            BOOST_TEST(!abs);
        }

        // Construct from source
        {
            test::fuse f;
            test::buffer_source bs(f);
            any_buffer_source abs(bs);
            BOOST_TEST(abs.has_value());
            BOOST_TEST(static_cast<bool>(abs));
        }
    }

    void
    testMove()
    {
        test::fuse f;
        test::buffer_source bs(f);

        any_buffer_source abs1(bs);
        BOOST_TEST(abs1.has_value());

        // Move construct
        any_buffer_source abs2(std::move(abs1));
        BOOST_TEST(abs2.has_value());
        BOOST_TEST(!abs1.has_value());

        // Move assign
        any_buffer_source abs3;
        abs3 = std::move(abs2);
        BOOST_TEST(abs3.has_value());
        BOOST_TEST(!abs2.has_value());
    }

    void
    testPull()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("hello world");

            any_buffer_source abs(bs);

            const_buffer arr[16];
            auto [ec, count] = co_await abs.pull(arr, 16);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(count, 1u);
            BOOST_TEST_EQ(arr[0].size(), 11u);
            abs.consume(11);
        });
        BOOST_TEST(r.success);
    }

    void
    testConsume()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("hello world");

            any_buffer_source abs(bs);

            const_buffer arr[16];

            // First pull returns all data
            auto [ec1, count1] = co_await abs.pull(arr, 16);
            if(ec1.failed())
                co_return;
            BOOST_TEST_EQ(count1, 1u);
            BOOST_TEST_EQ(arr[0].size(), 11u);

            // Consume partial (5 bytes = "hello")
            abs.consume(5);

            // Second pull returns remaining data
            auto [ec2, count2] = co_await abs.pull(arr, 16);
            if(ec2.failed())
                co_return;
            BOOST_TEST_EQ(count2, 1u);
            BOOST_TEST_EQ(arr[0].size(), 6u); // " world"

            // Consume rest
            abs.consume(6);

            // Third pull returns empty (exhausted)
            auto [ec3, count3] = co_await abs.pull(arr, 16);
            if(ec3.failed())
                co_return;
            BOOST_TEST_EQ(count3, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testPullWithoutConsume()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("test");

            any_buffer_source abs(bs);

            const_buffer arr[16];

            // Pull returns data
            auto [ec1, count1] = co_await abs.pull(arr, 16);
            if(ec1.failed())
                co_return;
            BOOST_TEST_EQ(count1, 1u);
            BOOST_TEST_EQ(arr[0].size(), 4u);

            // Pull again without consume returns same data
            auto [ec2, count2] = co_await abs.pull(arr, 16);
            if(ec2.failed())
                co_return;
            BOOST_TEST_EQ(count2, 1u);
            BOOST_TEST_EQ(arr[0].size(), 4u);

            abs.consume(4);
        });
        BOOST_TEST(r.success);
    }

    void
    testPullMultiple()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f, 5); // max 5 bytes per pull
            bs.provide("hello world");

            any_buffer_source abs(bs);

            std::size_t total = 0;
            for(;;)
            {
                const_buffer arr[16];
                auto [ec, count] = co_await abs.pull(arr, 16);
                if(ec.failed())
                    co_return;
                if(count == 0)
                    break;
                for(std::size_t i = 0; i < count; ++i)
                {
                    total += arr[i].size();
                    abs.consume(arr[i].size());
                }
            }

            BOOST_TEST_EQ(total, 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    testPullEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            // No data provided

            any_buffer_source abs(bs);

            const_buffer arr[16];
            auto [ec, count] = co_await abs.pull(arr, 16);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(count, 0u); // Source exhausted
        });
        BOOST_TEST(r.success);
    }

    void
    testPushTo()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("hello world");

            test::write_sink ws(f);

            auto [ec, n] = co_await push_to(bs, ws);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPushToTypeErased()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            bs.provide("hello world");

            any_buffer_source abs(bs);

            test::write_sink ws(f);

            auto [ec, n] = co_await push_to(abs, ws);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPushToChunked()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f, 5); // max 5 bytes per pull
            bs.provide("hello world");

            test::write_sink ws(f);

            auto [ec, n] = co_await push_to(bs, ws);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPushToEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_source bs(f);
            // No data provided

            test::write_sink ws(f);

            auto [ec, n] = co_await push_to(bs, ws);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testMove();
        testPull();
        testConsume();
        testPullWithoutConsume();
        testPullMultiple();
        testPullEmpty();
        testPushTo();
        testPushToTypeErased();
        testPushToChunked();
        testPushToEmpty();
    }
};

TEST_SUITE(any_buffer_source_test, "boost.capy.io.any_buffer_source");

} // namespace
} // namespace capy
} // namespace boost
