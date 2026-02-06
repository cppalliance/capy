//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/write_sink.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/write_sink.hpp>
#include <boost/capy/task.hpp>

#include "test/unit/test_helpers.hpp"

#include <array>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

static_assert(WriteSink<write_sink>);

class write_sink_test
{
public:
    void
    testConstruct()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            write_sink ws(f);
            BOOST_TEST(ws.size() == 0);
            BOOST_TEST(ws.data().empty());
            BOOST_TEST(! ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWrite()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec, n] = co_await ws.write(
                make_buffer("hello world", 11));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST(! ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec1, n1] = co_await ws.write(
                make_buffer("hello", 5));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 5u);

            auto [ec2, n2] = co_await ws.write(
                make_buffer(" ", 1));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 1u);

            auto [ec3, n3] = co_await ws.write(
                make_buffer("world", 5));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 5u);

            BOOST_TEST_EQ(ws.data(), "hello world");
            BOOST_TEST_EQ(ws.size(), 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteBufferSequence()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            std::array<const_buffer, 2> buffers = {{
                make_buffer("hello", 5),
                make_buffer("world", 5)
            }};

            auto [ec, n] = co_await ws.write(buffers);
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(ws.data(), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEmpty()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec, n] = co_await ws.write(const_buffer());
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(ws.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEofWithBuffers()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec, n] = co_await ws.write_eof(
                make_buffer("hello", 5));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(ws.data(), "hello");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEofWithEmptyBuffers()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec, n] = co_await ws.write_eof(const_buffer());
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
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec] = co_await ws.write_eof();
            if(ec)
                co_return;
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteThenWriteEof()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec1, n] = co_await ws.write(
                make_buffer("hello", 5));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST(! ws.eof_called());

            auto [ec2] = co_await ws.write_eof();
            if(ec2)
                co_return;
            BOOST_TEST(ws.eof_called());
            BOOST_TEST_EQ(ws.data(), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testFuseErrorInjection()
    {
        int write_success_count = 0;
        int write_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec, n] = co_await ws.write(
                make_buffer("test data", 9));
            if(ec)
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
    testFuseErrorInjectionWriteEof()
    {
        int eof_success_count = 0;
        int eof_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec] = co_await ws.write_eof();
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
    testExpect()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);
            auto ec = ws.expect("hello");
            BOOST_TEST(! ec);

            auto [ec2, n] = co_await ws.write(
                make_buffer("hello", 5));
            if(ec2)
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
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);
            auto ec = ws.expect("hello");
            BOOST_TEST(! ec);

            auto [ec2, n] = co_await ws.write(
                make_buffer("world", 5));
            if(! ec2)
                co_return;
            BOOST_TEST(ec2 == error::test_failure);
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectWithExistingData()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec, n] = co_await ws.write(
                make_buffer("hello", 5));
            if(ec)
                co_return;
            BOOST_TEST_EQ(ws.data(), "hello");

            auto ec2 = ws.expect("hello");
            BOOST_TEST(! ec2);
            BOOST_TEST(ws.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectMismatchWithExistingData()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec, n] = co_await ws.write(
                make_buffer("hello", 5));
            if(ec)
                co_return;

            auto ec2 = ws.expect("world");
            BOOST_TEST(ec2 == error::test_failure);
        });
        BOOST_TEST(r.success);
    }

    void
    testClear()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f);

            auto [ec1, n1] = co_await ws.write(
                make_buffer("hello", 5));
            if(ec1)
                co_return;

            auto [ec2] = co_await ws.write_eof();
            if(ec2)
                co_return;

            BOOST_TEST_EQ(ws.data(), "hello");
            BOOST_TEST(ws.eof_called());

            ws.clear();

            BOOST_TEST(ws.data().empty());
            BOOST_TEST(! ws.eof_called());

            auto [ec3, n2] = co_await ws.write(
                make_buffer("world", 5));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(ws.data(), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWritePartial()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f, 5); // max 5 bytes per write

            auto [ec, n] = co_await ws.write(
                make_buffer("hello world", 11));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(ws.data(), "hello");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteEofWithBuffersPartial()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            write_sink ws(f, 5); // max 5 bytes per write

            auto [ec, n] = co_await ws.write_eof(
                make_buffer("hello world", 11));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(ws.data(), "hello");
            BOOST_TEST(ws.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testWrite();
        testWriteMultiple();
        testWriteBufferSequence();
        testWriteEmpty();
        testWriteEofWithBuffers();
        testWriteEofWithEmptyBuffers();
        testWriteEof();
        testWriteThenWriteEof();
        testFuseErrorInjection();
        testFuseErrorInjectionWriteEof();
        testExpect();
        testExpectMismatch();
        testExpectWithExistingData();
        testExpectMismatchWithExistingData();
        testClear();
        testWritePartial();
        testWriteEofWithBuffersPartial();
    }
};

TEST_SUITE(write_sink_test, "boost.capy.test.write_sink");

} // test
} // capy
} // boost
