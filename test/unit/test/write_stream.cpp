//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/write_stream.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/task.hpp>

#include "../test_helpers.hpp"

#include <array>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

static_assert(WriteStream<write_stream>);

class write_stream_test
{
public:
    void
    testConstruct()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            write_stream ws(f);
            BOOST_TEST(ws.size() == 0);
            BOOST_TEST(ws.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSome()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            write_stream ws(f);

            auto [ec, n] = co_await ws.write_some(
                make_buffer("hello world", 11));
            if(ec.failed())
                co_return;
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            write_stream ws(f);

            auto [ec1, n1] = co_await ws.write_some(
                make_buffer("hello", 5));
            if(ec1.failed())
                co_return;
            BOOST_TEST_EQ(n1, 5u);

            auto [ec2, n2] = co_await ws.write_some(
                make_buffer(" ", 1));
            if(ec2.failed())
                co_return;
            BOOST_TEST_EQ(n2, 1u);

            auto [ec3, n3] = co_await ws.write_some(
                make_buffer("world", 5));
            if(ec3.failed())
                co_return;
            BOOST_TEST_EQ(n3, 5u);

            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST_EQ(ws.size(), 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeBufferSequence()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            write_stream ws(f);

            std::array<const_buffer, 2> buffers = {{
                make_buffer("hello", 5),
                make_buffer("world", 5)
            }};

            auto [ec, n] = co_await ws.write_some(buffers);
            if(ec.failed())
                co_return;
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeEmpty()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            write_stream ws(f);

            auto [ec, n] = co_await ws.write_some(const_buffer());
            if(ec.failed())
                co_return;
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testFuseErrorInjection()
    {
        int write_success_count = 0;
        int write_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            write_stream ws(f);

            auto [ec, n] = co_await ws.write_some(
                make_buffer("test data", 9));
            if(ec.failed())
            {
                ++write_error_count;
                co_return;
            }
            ++write_success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(write_error_count > 0);
        BOOST_TEST(write_success_count > 0);
    }

    void
    testExpect()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            write_stream ws(f);
            auto ec = ws.expect("hello");
            BOOST_TEST(! ec.failed());

            auto [ec2, n] = co_await ws.write_some(
                make_buffer("hello", 5));
            if(ec2.failed())
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST(ws.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectMismatch()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            write_stream ws(f);
            auto ec = ws.expect("hello");
            BOOST_TEST(! ec.failed());

            auto [ec2, n] = co_await ws.write_some(
                make_buffer("world", 5));
            if(! ec2.failed())
                co_return;
            BOOST_TEST(ec2 == error::test_failure);
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectWithExistingData()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            write_stream ws(f);

            auto [ec, n] = co_await ws.write_some(
                make_buffer("hello", 5));
            if(ec.failed())
                co_return;
            BOOST_TEST_EQ(ws.data(), "hello");

            auto ec2 = ws.expect("hello");
            BOOST_TEST(! ec2.failed());
            BOOST_TEST(ws.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectMismatchWithExistingData()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            write_stream ws(f);

            auto [ec, n] = co_await ws.write_some(
                make_buffer("hello", 5));
            if(ec.failed())
                co_return;

            auto ec2 = ws.expect("world");
            BOOST_TEST(ec2 == error::test_failure);
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectPartialMatch()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            write_stream ws(f);
            auto ec = ws.expect("helloworld");
            BOOST_TEST(! ec.failed());

            auto [ec2, n] = co_await ws.write_some(
                make_buffer("hello", 5));
            if(ec2.failed())
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST(ws.data().empty());

            auto [ec3, n2] = co_await ws.write_some(
                make_buffer("world", 5));
            if(ec3.failed())
                co_return;
            BOOST_TEST_EQ(n2, 5u);
            BOOST_TEST(ws.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectExcessData()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            write_stream ws(f);
            auto ec = ws.expect("hi");
            BOOST_TEST(! ec.failed());

            auto [ec2, n] = co_await ws.write_some(
                make_buffer("hi there", 8));
            if(ec2.failed())
                co_return;
            BOOST_TEST_EQ(n, 8u);
            BOOST_TEST_EQ(ws.data(), " there");
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testWriteSome();
        testWriteSomeMultiple();
        testWriteSomeBufferSequence();
        testWriteSomeEmpty();
        testFuseErrorInjection();
        testExpect();
        testExpectMismatch();
        testExpectWithExistingData();
        testExpectMismatchWithExistingData();
        testExpectPartialMatch();
        testExpectExcessData();
    }
};

TEST_SUITE(write_stream_test, "boost.capy.test.write_stream");

} // test
} // capy
} // boost
