//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/read_stream.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>

#include "../test_helpers.hpp"

#include <array>
#include <cstring>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

static_assert(ReadStream<read_stream>);

class read_stream_test
{
public:
    void
    testConstruct()
    {
        fuse f;
        read_stream rs(f);
        BOOST_TEST(rs.available() == 0);
    }

    void
    testProvide()
    {
        fuse f;
        read_stream rs(f);

        rs.provide("hello");
        BOOST_TEST(rs.available() == 5);

        rs.provide(" world");
        BOOST_TEST(rs.available() == 11);
    }

    void
    testClear()
    {
        fuse f;
        read_stream rs(f);

        rs.provide("data");
        BOOST_TEST(rs.available() == 4);

        rs.clear();
        BOOST_TEST(rs.available() == 0);
    }

    void
    testReadSome()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        fuse f;
        read_stream rs(f);
        rs.provide("hello world");

        auto do_test = [&]() -> task<void>
        {
            char buf[32] = {};
            auto [ec, n] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(!ec.failed());
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadSomePartial()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        fuse f;
        read_stream rs(f);
        rs.provide("hello world");

        auto do_test = [&]() -> task<void>
        {
            char buf[5] = {};
            auto [ec, n] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(!ec.failed());
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello");
            BOOST_TEST_EQ(rs.available(), 6u);
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadSomeMultiple()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        fuse f;
        read_stream rs(f);
        rs.provide("abcdefghij");

        auto do_test = [&]() -> task<void>
        {
            char buf[3] = {};

            auto [ec1, n1] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(!ec1.failed());
            BOOST_TEST_EQ(n1, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abc");

            auto [ec2, n2] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(!ec2.failed());
            BOOST_TEST_EQ(n2, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "def");

            auto [ec3, n3] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(!ec3.failed());
            BOOST_TEST_EQ(n3, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n3), "ghi");

            auto [ec4, n4] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(!ec4.failed());
            BOOST_TEST_EQ(n4, 1u);
            BOOST_TEST_EQ(std::string_view(buf, n4), "j");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadSomeEof()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        fuse f;
        read_stream rs(f);
        // No data provided - immediate EOF

        auto do_test = [&]() -> task<void>
        {
            char buf[32] = {};
            auto [ec, n] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 0u);
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadSomeEofAfterData()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        fuse f;
        read_stream rs(f);
        rs.provide("x");

        auto do_test = [&]() -> task<void>
        {
            char buf[32] = {};

            auto [ec1, n1] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(!ec1.failed());
            BOOST_TEST_EQ(n1, 1u);

            auto [ec2, n2] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(ec2 == cond::eof);
            BOOST_TEST_EQ(n2, 0u);
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testReadSomeBufferSequence()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        fuse f;
        read_stream rs(f);
        rs.provide("helloworld");

        auto do_test = [&]() -> task<void>
        {
            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                make_buffer(buf1),
                make_buffer(buf2)
            }};

            auto [ec, n] = co_await rs.read_some(buffers);
            BOOST_TEST(!ec.failed());
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(buf2, 5), "world");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testFuseErrorInjection()
    {
        fuse f;
        read_stream rs(f);
        rs.provide("test data");

        int read_success_count = 0;
        int read_error_count = 0;

        auto r = f.armed([&](fuse&) {
            char buf[32] = {};
            auto result = rs.read_some(make_buffer(buf));
            auto [ec, n] = result.await_resume();

            if(ec.failed())
            {
                ++read_error_count;
                rs.clear();
                rs.provide("test data");
                return;
            }
            ++read_success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(read_error_count > 0);
        BOOST_TEST(read_success_count > 0);
    }

    void
    testClearAndReuse()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        fuse f;
        read_stream rs(f);
        rs.provide("first");

        auto do_test = [&]() -> task<void>
        {
            char buf[32] = {};

            auto [ec1, n1] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(!ec1.failed());
            BOOST_TEST_EQ(std::string_view(buf, n1), "first");

            rs.clear();
            rs.provide("second");

            auto [ec2, n2] = co_await rs.read_some(make_buffer(buf));
            BOOST_TEST(!ec2.failed());
            BOOST_TEST_EQ(std::string_view(buf, n2), "second");
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    testEmptyBuffer()
    {
        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        bool completed = false;

        fuse f;
        read_stream rs(f);
        rs.provide("data");

        auto do_test = [&]() -> task<void>
        {
            auto [ec, n] = co_await rs.read_some(mutable_buffer());
            BOOST_TEST(!ec.failed());
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST_EQ(rs.available(), 4u);
        };

        run_async(ex,
            [&]() { completed = true; },
            [](std::exception_ptr) {})(do_test());

        BOOST_TEST(completed);
    }

    void
    run()
    {
        testConstruct();
        testProvide();
        testClear();
        testReadSome();
        testReadSomePartial();
        testReadSomeMultiple();
        testReadSomeEof();
        testReadSomeEofAfterData();
        testReadSomeBufferSequence();
        testFuseErrorInjection();
        testClearAndReuse();
        testEmptyBuffer();
    }
};

TEST_SUITE(read_stream_test, "boost.capy.test.read_stream");

} // test
} // capy
} // boost
