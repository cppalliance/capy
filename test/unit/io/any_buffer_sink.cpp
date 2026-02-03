//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_buffer_sink.hpp>

// Test that pull_from header is self-contained.
#include <boost/capy/io/pull_from.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/buffer_sink.hpp>
#include <boost/capy/test/read_source.hpp>
#include <boost/capy/test/read_stream.hpp>

#include "test/unit/test_helpers.hpp"

#include <cstring>
#include <string_view>

namespace boost {
namespace capy {
namespace {

// Static assert that any_buffer_sink satisfies BufferSink
static_assert(BufferSink<any_buffer_sink>);

class any_buffer_sink_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_buffer_sink abs;
            BOOST_TEST(!abs.has_value());
            BOOST_TEST(!abs);
        }

        // Construct from sink
        {
            test::fuse f;
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);
            BOOST_TEST(abs.has_value());
            BOOST_TEST(static_cast<bool>(abs));
        }
    }

    void
    testMove()
    {
        test::fuse f;
        test::buffer_sink bs(f);

        any_buffer_sink abs1(&bs);
        BOOST_TEST(abs1.has_value());

        // Move construct
        any_buffer_sink abs2(std::move(abs1));
        BOOST_TEST(abs2.has_value());
        BOOST_TEST(!abs1.has_value());

        // Move assign
        any_buffer_sink abs3;
        abs3 = std::move(abs2);
        BOOST_TEST(abs3.has_value());
        BOOST_TEST(!abs2.has_value());
    }

    void
    testPrepareCommit()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            mutable_buffer arr[detail::max_iovec_];
            auto bufs = abs.prepare(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST(bufs[0].size() > 0);

            // Write data into the buffer
            std::memcpy(bufs[0].data(), "hello", 5);

            auto [ec] = co_await abs.commit(5);
            if(ec)
                co_return;

            BOOST_TEST_EQ(bs.data(), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testCommitWithEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            mutable_buffer arr[detail::max_iovec_];
            auto bufs = abs.prepare(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);

            std::memcpy(bufs[0].data(), "world", 5);

            auto [ec] = co_await abs.commit(5, true);
            if(ec)
                co_return;

            BOOST_TEST_EQ(bs.data(), "world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testCommitEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            mutable_buffer arr[detail::max_iovec_];
            auto bufs = abs.prepare(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);

            std::memcpy(bufs[0].data(), "data", 4);

            auto [ec1] = co_await abs.commit(4);
            if(ec1)
                co_return;

            auto [ec2] = co_await abs.commit_eof();
            if(ec2)
                co_return;

            BOOST_TEST_EQ(bs.data(), "data");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testMultipleCommits()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            // First write
            {
                mutable_buffer arr[detail::max_iovec_];
                auto bufs = abs.prepare(arr);
                BOOST_TEST_EQ(bufs.size(), 1u);

                std::memcpy(bufs[0].data(), "hello ", 6);

                auto [ec] = co_await abs.commit(6);
                if(ec)
                    co_return;
            }

            // Second write
            {
                mutable_buffer arr[detail::max_iovec_];
                auto bufs = abs.prepare(arr);
                BOOST_TEST_EQ(bufs.size(), 1u);

                std::memcpy(bufs[0].data(), "world", 5);

                auto [ec] = co_await abs.commit(5, true);
                if(ec)
                    co_return;
            }

            BOOST_TEST_EQ(bs.data(), "hello world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testEmptyCommit()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::buffer_sink bs(f);
            any_buffer_sink abs(&bs);

            auto [ec] = co_await abs.commit_eof();
            if(ec)
                co_return;

            BOOST_TEST(bs.data().empty());
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadStream()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream src(f);
            src.provide("hello world");

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadStreamTypeErased()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream src(f);
            src.provide("hello world");

            test::buffer_sink sink(f);
            any_buffer_sink abs(&sink);

            auto [ec, n] = co_await pull_from(src, abs);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadStreamChunked()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream src(f, 5); // max 5 bytes per read
            src.provide("hello world");

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadStreamEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream src(f);
            // No data provided

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(sink.data().empty());
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadSource()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source src(f);
            src.provide("hello world");

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadSourceTypeErased()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source src(f);
            src.provide("hello world");

            test::buffer_sink sink(f);
            any_buffer_sink abs(&sink);

            auto [ec, n] = co_await pull_from(src, abs);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadSourceChunked()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source src(f, 5); // max 5 bytes per read
            src.provide("hello world");

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(sink.data(), "hello world");
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullFromReadSourceEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source src(f);
            // No data provided

            test::buffer_sink sink(f);

            auto [ec, n] = co_await pull_from(src, sink);
            if(ec)
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(sink.data().empty());
            BOOST_TEST(sink.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testMove();
        testPrepareCommit();
        testCommitWithEof();
        testCommitEof();
        testMultipleCommits();
        testEmptyCommit();
        testPullFromReadStream();
        testPullFromReadStreamTypeErased();
        testPullFromReadStreamChunked();
        testPullFromReadStreamEmpty();
        testPullFromReadSource();
        testPullFromReadSourceTypeErased();
        testPullFromReadSourceChunked();
        testPullFromReadSourceEmpty();
    }
};

TEST_SUITE(any_buffer_sink_test, "boost.capy.io.any_buffer_sink");

} // namespace
} // namespace capy
} // namespace boost
