//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/buffer_source.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/buffer_source.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/buffer_to_string.hpp>

#include "test/unit/test_helpers.hpp"

#include <string_view>

namespace boost {
namespace capy {
namespace test {

static_assert(BufferSource<buffer_source>);

class buffer_source_test
{
public:
    void
    testConstruct()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            buffer_source bs(f);
            BOOST_TEST_EQ(bs.available(), 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testProvide()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            buffer_source bs(f);
            bs.provide("hello");
            BOOST_TEST_EQ(bs.available(), 5u);

            bs.provide(" world");
            BOOST_TEST_EQ(bs.available(), 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    testClear()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            buffer_source bs(f);
            bs.provide("data");
            BOOST_TEST_EQ(bs.available(), 4u);

            bs.clear();
            BOOST_TEST_EQ(bs.available(), 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testPull()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_source bs(f);
            bs.provide("hello world");

            const_buffer arr[16];
            auto [ec, bufs] = co_await bs.pull(arr);
            if(ec)
                co_return;

            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs[0].size(), 11u);
            BOOST_TEST_EQ(
                buffer_to_string(bufs), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testConsume()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_source bs(f);
            bs.provide("hello world");

            const_buffer arr[16];

            auto [ec1, bufs1] = co_await bs.pull(arr);
            if(ec1)
                co_return;
            BOOST_TEST_EQ(bufs1.size(), 1u);
            BOOST_TEST_EQ(bufs1[0].size(), 11u);

            bs.consume(5);
            BOOST_TEST_EQ(bs.available(), 6u);

            auto [ec2, bufs2] = co_await bs.pull(arr);
            if(ec2)
                co_return;
            BOOST_TEST_EQ(bufs2.size(), 1u);
            BOOST_TEST_EQ(bufs2[0].size(), 6u);
            BOOST_TEST_EQ(
                buffer_to_string(bufs2), " world");

            bs.consume(6);

            auto [ec3, bufs3] = co_await bs.pull(arr);
            if(ec3 != cond::eof)
                co_return;
            BOOST_TEST(bufs3.empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullWithoutConsume()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_source bs(f);
            bs.provide("test");

            const_buffer arr[16];

            auto [ec1, bufs1] = co_await bs.pull(arr);
            if(ec1)
                co_return;
            BOOST_TEST_EQ(bufs1.size(), 1u);
            BOOST_TEST_EQ(bufs1[0].size(), 4u);

            auto [ec2, bufs2] = co_await bs.pull(arr);
            if(ec2)
                co_return;
            BOOST_TEST_EQ(bufs2.size(), 1u);
            BOOST_TEST_EQ(bufs2[0].size(), 4u);

            bs.consume(4);
        });
        BOOST_TEST(r.success);
    }

    void
    testPullEmpty()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_source bs(f);

            const_buffer arr[16];
            auto [ec, bufs] = co_await bs.pull(arr);
            if(ec != cond::eof)
                co_return;
            BOOST_TEST(bufs.empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testPullEmptyDest()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_source bs(f);
            bs.provide("data");

            std::span<const_buffer> empty_span;
            auto [ec, bufs] = co_await bs.pull(empty_span);
            if(ec)
                co_return;
            BOOST_TEST(bufs.empty());
            BOOST_TEST_EQ(bs.available(), 4u);
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxPullSize()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_source bs(f, 5);
            bs.provide("hello world");

            const_buffer arr[16];
            auto [ec, bufs] = co_await bs.pull(arr);
            if(ec)
                co_return;

            BOOST_TEST_EQ(bufs.size(), 1u);
            BOOST_TEST_EQ(bufs[0].size(), 5u);
            BOOST_TEST_EQ(
                buffer_to_string(bufs), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxPullSizeMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_source bs(f, 5);
            bs.provide("hello world");

            std::size_t total = 0;
            for(;;)
            {
                const_buffer arr[16];
                auto [ec, bufs] = co_await bs.pull(arr);
                if(ec == cond::eof)
                    break;
                if(ec)
                    co_return;
                for(auto const& buf : bufs)
                {
                    total += buf.size();
                    bs.consume(buf.size());
                }
            }

            BOOST_TEST_EQ(total, 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    testFuseErrorInjection()
    {
        int pull_success_count = 0;
        int pull_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_source bs(f);
            bs.provide("test data");

            const_buffer arr[16];
            auto [ec, bufs] = co_await bs.pull(arr);
            if(ec)
            {
                ++pull_error_count;
                co_return;
            }
            ++pull_success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(pull_error_count > 0);
        BOOST_TEST(pull_success_count > 0);
    }

    void
    testClearAndReuse()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            buffer_source bs(f);
            bs.provide("first");

            const_buffer arr[16];

            auto [ec1, bufs1] = co_await bs.pull(arr);
            if(ec1)
                co_return;
            BOOST_TEST_EQ(
                buffer_to_string(bufs1), "first");

            bs.consume(5);
            bs.clear();
            bs.provide("second");

            auto [ec2, bufs2] = co_await bs.pull(arr);
            if(ec2)
                co_return;
            BOOST_TEST_EQ(
                buffer_to_string(bufs2), "second");
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testProvide();
        testClear();
        testPull();
        testConsume();
        testPullWithoutConsume();
        testPullEmpty();
        testPullEmptyDest();
        testMaxPullSize();
        testMaxPullSizeMultiple();
        testFuseErrorInjection();
        testClearAndReuse();
    }
};

TEST_SUITE(buffer_source_test, "boost.capy.test.buffer_source");

} // test
} // capy
} // boost
