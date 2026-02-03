//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/stream.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/task.hpp>

#include "test/unit/test_helpers.hpp"

#include <array>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

static_assert(ReadStream<stream>);
static_assert(WriteStream<stream>);

class stream_test
{
public:
    void
    testConstruct()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            stream s(f);
            BOOST_TEST(s.available() == 0);
            BOOST_TEST(s.size() == 0);
            BOOST_TEST(s.data().empty());
        });
        BOOST_TEST(r.success);
    }

    //--------------------------------------------
    //
    // Read operations
    //
    //--------------------------------------------

    void
    testProvide()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            stream s(f);
            s.provide("hello");
            BOOST_TEST_EQ(s.available(), 5u);

            s.provide(" world");
            BOOST_TEST_EQ(s.available(), 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    testClear()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            stream s(f);
            s.provide("data");
            BOOST_TEST_EQ(s.available(), 4u);

            s.clear();
            BOOST_TEST_EQ(s.available(), 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSome()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            stream s(f);
            s.provide("hello world");

            char buf[32] = {};
            auto [ec, n] = co_await s.read_some(make_buffer(buf));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomePartial()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            stream s(f);
            s.provide("hello world");

            char buf[5] = {};
            auto [ec, n] = co_await s.read_some(make_buffer(buf));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello");
            BOOST_TEST_EQ(s.available(), 6u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            stream s(f);
            s.provide("abcdefghij");

            char buf[3] = {};

            auto [ec1, n1] = co_await s.read_some(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abc");

            auto [ec2, n2] = co_await s.read_some(make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "def");

            auto [ec3, n3] = co_await s.read_some(make_buffer(buf));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n3), "ghi");

            auto [ec4, n4] = co_await s.read_some(make_buffer(buf));
            if(ec4)
                co_return;
            BOOST_TEST_EQ(n4, 1u);
            BOOST_TEST_EQ(std::string_view(buf, n4), "j");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeEof()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            stream s(f);

            char buf[32] = {};
            auto [ec, n] = co_await s.read_some(make_buffer(buf));
            if(ec && ec != cond::eof)
                co_return;
            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeEofAfterData()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            stream s(f);
            s.provide("x");

            char buf[32] = {};

            auto [ec1, n1] = co_await s.read_some(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 1u);

            auto [ec2, n2] = co_await s.read_some(make_buffer(buf));
            if(ec2 && ec2 != cond::eof)
                co_return;
            BOOST_TEST(ec2 == cond::eof);
            BOOST_TEST_EQ(n2, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeBufferSequence()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            stream s(f);
            s.provide("helloworld");

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                make_buffer(buf1),
                make_buffer(buf2)
            }};

            auto [ec, n] = co_await s.read_some(buffers);
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(buf2, 5), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSomeEmpty()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            stream s(f);
            s.provide("data");

            auto [ec, n] = co_await s.read_some(mutable_buffer());
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST_EQ(s.available(), 4u);
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxReadSize()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            stream s(f, 3);
            s.provide("hello world");

            char buf[32] = {};
            auto [ec, n] = co_await s.read_some(make_buffer(buf));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hel");
            BOOST_TEST_EQ(s.available(), 8u);
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxReadSizeMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            stream s(f, 4);
            s.provide("abcdefghij");

            char buf[32] = {};

            auto [ec1, n1] = co_await s.read_some(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 4u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abcd");

            auto [ec2, n2] = co_await s.read_some(make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 4u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "efgh");

            auto [ec3, n3] = co_await s.read_some(make_buffer(buf));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 2u);
            BOOST_TEST_EQ(std::string_view(buf, n3), "ij");
        });
        BOOST_TEST(r.success);
    }

    //--------------------------------------------
    //
    // Write operations
    //
    //--------------------------------------------

    void
    testWriteSome()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);

            auto [ec, n] = co_await s.write_some(
                make_buffer("hello world", 11));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(s.data(), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);

            auto [ec1, n1] = co_await s.write_some(
                make_buffer("hello", 5));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 5u);

            auto [ec2, n2] = co_await s.write_some(
                make_buffer(" ", 1));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 1u);

            auto [ec3, n3] = co_await s.write_some(
                make_buffer("world", 5));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 5u);

            BOOST_TEST_EQ(s.data(), "hello world");
            BOOST_TEST_EQ(s.size(), 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeBufferSequence()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);

            std::array<const_buffer, 2> buffers = {{
                make_buffer("hello", 5),
                make_buffer("world", 5)
            }};

            auto [ec, n] = co_await s.write_some(buffers);
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(s.data(), "helloworld");
        });
        BOOST_TEST(r.success);
    }

    void
    testWriteSomeEmpty()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);

            auto [ec, n] = co_await s.write_some(const_buffer());
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(s.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testExpect()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);
            auto ec = s.expect("hello");
            BOOST_TEST(! ec);

            auto [ec2, n] = co_await s.write_some(
                make_buffer("hello", 5));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST(s.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectMismatch()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);
            auto ec = s.expect("hello");
            BOOST_TEST(! ec);

            auto [ec2, n] = co_await s.write_some(
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
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);

            auto [ec, n] = co_await s.write_some(
                make_buffer("hello", 5));
            if(ec)
                co_return;
            BOOST_TEST_EQ(s.data(), "hello");

            auto ec2 = s.expect("hello");
            BOOST_TEST(! ec2);
            BOOST_TEST(s.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectMismatchWithExistingData()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);

            auto [ec, n] = co_await s.write_some(
                make_buffer("hello", 5));
            if(ec)
                co_return;

            auto ec2 = s.expect("world");
            BOOST_TEST(ec2 == error::test_failure);
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectPartialMatch()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);
            auto ec = s.expect("helloworld");
            BOOST_TEST(! ec);

            auto [ec2, n] = co_await s.write_some(
                make_buffer("hello", 5));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST(s.data().empty());

            auto [ec3, n2] = co_await s.write_some(
                make_buffer("world", 5));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n2, 5u);
            BOOST_TEST(s.data().empty());
        });
        BOOST_TEST(r.success);
    }

    void
    testExpectExcessData()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);
            auto ec = s.expect("hi");
            BOOST_TEST(! ec);

            auto [ec2, n] = co_await s.write_some(
                make_buffer("hi there", 8));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n, 8u);
            BOOST_TEST_EQ(s.data(), " there");
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxWriteSize()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f, std::size_t(-1), 3);

            auto [ec, n] = co_await s.write_some(
                make_buffer("hello world", 11));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 3u);
            BOOST_TEST_EQ(s.data(), "hel");
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxWriteSizeMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f, std::size_t(-1), 4);

            auto [ec1, n1] = co_await s.write_some(
                make_buffer("abcdefghij", 10));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 4u);
            BOOST_TEST_EQ(s.data(), "abcd");

            auto [ec2, n2] = co_await s.write_some(
                make_buffer("efghij", 6));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 4u);
            BOOST_TEST_EQ(s.data(), "abcdefgh");

            auto [ec3, n3] = co_await s.write_some(
                make_buffer("ij", 2));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 2u);
            BOOST_TEST_EQ(s.data(), "abcdefghij");
        });
        BOOST_TEST(r.success);
    }

    //--------------------------------------------
    //
    // Combined operations
    //
    //--------------------------------------------

    void
    testReadWrite()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);
            s.provide("request");

            char buf[32] = {};
            auto [ec1, n1] = co_await s.read_some(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 7u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "request");

            auto [ec2, n2] = co_await s.write_some(
                make_buffer("response", 8));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 8u);
            BOOST_TEST_EQ(s.data(), "response");
        });
        BOOST_TEST(r.success);
    }

    void
    testLoopback()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f, 4, 4);
            s.provide("hello world!");

            std::string received;
            char buf[4];

            while(s.available() > 0)
            {
                auto [ec, n] = co_await s.read_some(make_buffer(buf));
                if(ec)
                    co_return;
                auto [ec2, n2] = co_await s.write_some(
                    make_buffer(buf, n));
                if(ec2)
                    co_return;
            }

            BOOST_TEST_EQ(s.data(), "hello world!");
        });
        BOOST_TEST(r.success);
    }

    void
    testFuseReadErrorInjection()
    {
        int read_success_count = 0;
        int read_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            stream s(f);
            s.provide("test data");

            char buf[32] = {};
            auto [ec, n] = co_await s.read_some(make_buffer(buf));
            if(ec)
            {
                ++read_error_count;
                co_return;
            }
            ++read_success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(read_error_count > 0);
        BOOST_TEST(read_success_count > 0);
    }

    void
    testFuseWriteErrorInjection()
    {
        int write_success_count = 0;
        int write_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<void> {
            stream s(f);

            auto [ec, n] = co_await s.write_some(
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
    testClearAndReuse()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            stream s(f);
            s.provide("first");

            char buf[32] = {};

            auto [ec1, n1] = co_await s.read_some(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(std::string_view(buf, n1), "first");

            s.clear();
            s.provide("second");

            auto [ec2, n2] = co_await s.read_some(make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(std::string_view(buf, n2), "second");
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();

        // Read operations
        testProvide();
        testClear();
        testReadSome();
        testReadSomePartial();
        testReadSomeMultiple();
        testReadSomeEof();
        testReadSomeEofAfterData();
        testReadSomeBufferSequence();
        testReadSomeEmpty();
        testMaxReadSize();
        testMaxReadSizeMultiple();

        // Write operations
        testWriteSome();
        testWriteSomeMultiple();
        testWriteSomeBufferSequence();
        testWriteSomeEmpty();
        testExpect();
        testExpectMismatch();
        testExpectWithExistingData();
        testExpectMismatchWithExistingData();
        testExpectPartialMatch();
        testExpectExcessData();
        testMaxWriteSize();
        testMaxWriteSizeMultiple();

        // Combined operations
        testReadWrite();
        testLoopback();
        testFuseReadErrorInjection();
        testFuseWriteErrorInjection();
        testClearAndReuse();
    }
};

TEST_SUITE(stream_test, "boost.capy.test.stream");

} // test
} // capy
} // boost
