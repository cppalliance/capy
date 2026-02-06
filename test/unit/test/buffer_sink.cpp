//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/buffer_sink.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/buffer_sink.hpp>
#include <boost/capy/task.hpp>

#include "test/unit/test_helpers.hpp"

#include <cstring>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

static_assert(BufferSink<buffer_sink>);

class buffer_sink_test
{
public:
    void
    testConstruct()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            buffer_sink bs(f);
            BOOST_TEST_EQ(bs.size(), 0u);
            BOOST_TEST(bs.data().empty());
            BOOST_TEST(! bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testPrepareCommit()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_sink bs(f);

            mutable_buffer arr[16];
            auto bufs = bs.prepare(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST(bufs[0].size() > 0);

            std::memcpy(bufs[0].data(), "hello", 5);

            auto [ec] = co_await bs.commit(5);
            if(ec)
                co_return;

            BOOST_TEST_EQ(bs.data(), "hello");
            BOOST_TEST_EQ(bs.size(), 5u);
        });
        BOOST_TEST(r.success);
    }

    void
    testPrepareEmpty()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            buffer_sink bs(f);

            std::span<mutable_buffer> empty_span;
            auto bufs = bs.prepare(empty_span);
            BOOST_TEST(bufs.empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testMultipleCommits()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_sink bs(f);

            {
                mutable_buffer arr[16];
                auto bufs = bs.prepare(arr);
                std::memcpy(bufs[0].data(), "hello ", 6);
                auto [ec] = co_await bs.commit(6);
                if(ec)
                    co_return;
            }

            {
                mutable_buffer arr[16];
                auto bufs = bs.prepare(arr);
                std::memcpy(bufs[0].data(), "world", 5);
                auto [ec] = co_await bs.commit(5);
                if(ec)
                    co_return;
            }

            BOOST_TEST_EQ(bs.data(), "hello world");
            BOOST_TEST_EQ(bs.size(), 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    testCommitWithEof()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_sink bs(f);

            mutable_buffer arr[16];
            auto bufs = bs.prepare(arr);
            std::memcpy(bufs[0].data(), "data", 4);

            auto [ec] = co_await bs.commit_eof(4);
            if(ec)
                co_return;

            BOOST_TEST_EQ(bs.data(), "data");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testCommitEof()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_sink bs(f);

            auto [ec] = co_await bs.commit_eof(0);
            if(ec)
                co_return;

            BOOST_TEST(bs.data().empty());
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testCommitThenEof()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_sink bs(f);

            mutable_buffer arr[16];
            auto bufs = bs.prepare(arr);
            std::memcpy(bufs[0].data(), "hello", 5);

            auto [ec1] = co_await bs.commit(5);
            if(ec1)
                co_return;
            BOOST_TEST(! bs.eof_called());

            auto [ec2] = co_await bs.commit_eof(0);
            if(ec2)
                co_return;
            BOOST_TEST(bs.eof_called());
            BOOST_TEST_EQ(bs.data(), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxPrepareSize()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_sink bs(f, 8);

            mutable_buffer arr[16];
            auto bufs = bs.prepare(arr);
            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs[0].size(), 8u);

            std::memcpy(bufs[0].data(), "12345678", 8);

            auto [ec] = co_await bs.commit(8);
            if(ec)
                co_return;

            BOOST_TEST_EQ(bs.data(), "12345678");
        });
        BOOST_TEST(r.success);
    }

    void
    testClear()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_sink bs(f);

            mutable_buffer arr[16];
            auto bufs = bs.prepare(arr);
            std::memcpy(bufs[0].data(), "hello", 5);

            auto [ec1] = co_await bs.commit(5);
            if(ec1)
                co_return;

            auto [ec2] = co_await bs.commit_eof(0);
            if(ec2)
                co_return;

            BOOST_TEST_EQ(bs.data(), "hello");
            BOOST_TEST(bs.eof_called());

            bs.clear();

            BOOST_TEST(bs.data().empty());
            BOOST_TEST_EQ(bs.size(), 0u);
            BOOST_TEST(! bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testFuseErrorInjectionCommit()
    {
        int commit_success_count = 0;
        int commit_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_sink bs(f);

            mutable_buffer arr[16];
            auto bufs = bs.prepare(arr);
            std::memcpy(bufs[0].data(), "data", 4);

            auto [ec] = co_await bs.commit(4);
            if(ec)
            {
                ++commit_error_count;
                co_return;
            }
            ++commit_success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(commit_error_count > 0);
        BOOST_TEST(commit_success_count > 0);
    }

    void
    testFuseErrorInjectionCommitEof()
    {
        int eof_success_count = 0;
        int eof_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_sink bs(f);

            auto [ec] = co_await bs.commit_eof(0);
            if(ec)
            {
                ++eof_error_count;
                co_return;
            }
            ++eof_success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(eof_error_count > 0);
        BOOST_TEST(eof_success_count > 0);
    }

    void
    run()
    {
        testConstruct();
        testPrepareCommit();
        testPrepareEmpty();
        testMultipleCommits();
        testCommitWithEof();
        testCommitEof();
        testCommitThenEof();
        testMaxPrepareSize();
        testClear();
        testFuseErrorInjectionCommit();
        testFuseErrorInjectionCommitEof();
    }
};

TEST_SUITE(buffer_sink_test, "boost.capy.test.buffer_sink");

} // test
} // capy
} // boost
