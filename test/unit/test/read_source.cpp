//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/read_source.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/read_source.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/task.hpp>

#include "test/unit/test_helpers.hpp"

#include <array>
#include <string_view>

namespace boost {
namespace capy {
namespace test {

static_assert(ReadSource<read_source>);

class read_source_test
{
public:
    void
    testConstruct()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            read_source rs(f);
            BOOST_TEST(rs.available() == 0);
        });
        BOOST_TEST(r.success);
    }

    void
    testProvide()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            read_source rs(f);
            rs.provide("hello");
            BOOST_TEST_EQ(rs.available(), 5u);

            rs.provide(" world");
            BOOST_TEST_EQ(rs.available(), 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    testClear()
    {
        fuse f;
        auto r = f.armed([&](fuse&) {
            read_source rs(f);
            rs.provide("data");
            BOOST_TEST_EQ(rs.available(), 4u);

            rs.clear();
            BOOST_TEST_EQ(rs.available(), 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testRead()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            read_source rs(f);
            rs.provide("hello world");

            char buf[32] = {};
            auto [ec, n] = co_await rs.read(make_buffer(buf));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadPartial()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            read_source rs(f);
            rs.provide("hello world");

            char buf[5] = {};
            auto [ec, n] = co_await rs.read(make_buffer(buf));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello");
            BOOST_TEST_EQ(rs.available(), 6u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            read_source rs(f);
            rs.provide("abcdefghij");

            char buf[3] = {};

            auto [ec1, n1] = co_await rs.read(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abc");

            auto [ec2, n2] = co_await rs.read(make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "def");

            auto [ec3, n3] = co_await rs.read(make_buffer(buf));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n3), "ghi");

            auto [ec4, n4] = co_await rs.read(make_buffer(buf));
            if(ec4)
                co_return;
            BOOST_TEST_EQ(n4, 1u);
            BOOST_TEST_EQ(std::string_view(buf, n4), "j");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadEof()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            read_source rs(f);

            char buf[32] = {};
            auto [ec, n] = co_await rs.read(make_buffer(buf));
            if(ec && ec != cond::eof)
                co_return;
            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadEofAfterData()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            read_source rs(f);
            rs.provide("x");

            char buf[32] = {};

            auto [ec1, n1] = co_await rs.read(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 1u);

            auto [ec2, n2] = co_await rs.read(make_buffer(buf));
            if(ec2 && ec2 != cond::eof)
                co_return;
            BOOST_TEST(ec2 == cond::eof);
            BOOST_TEST_EQ(n2, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadBufferSequence()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            read_source rs(f);
            rs.provide("helloworld");

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                make_buffer(buf1),
                make_buffer(buf2)
            }};

            auto [ec, n] = co_await rs.read(buffers);
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(buf2, 5), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadEmpty()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            read_source rs(f);
            rs.provide("data");

            auto [ec, n] = co_await rs.read(mutable_buffer());
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST_EQ(rs.available(), 4u);
        });
        BOOST_TEST(r.success);
    }

    void
    testFuseErrorInjection()
    {
        int read_success_count = 0;
        int read_error_count = 0;

        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            read_source rs(f);
            rs.provide("test data");

            char buf[32] = {};
            auto [ec, n] = co_await rs.read(make_buffer(buf));
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
    testClearAndReuse()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            read_source rs(f);
            rs.provide("first");

            char buf[32] = {};

            auto [ec1, n1] = co_await rs.read(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(std::string_view(buf, n1), "first");

            rs.clear();
            rs.provide("second");

            auto [ec2, n2] = co_await rs.read(make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(std::string_view(buf, n2), "second");
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxReadSize()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            read_source rs(f, 5); // max 5 bytes per read
            rs.provide("hello world");

            char buf[32] = {};
            auto [ec, n] = co_await rs.read(make_buffer(buf));
            if(ec)
                co_return;
            BOOST_TEST_EQ(n, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello");
            BOOST_TEST_EQ(rs.available(), 6u);
        });
        BOOST_TEST(r.success);
    }

    void
    testMaxReadSizeMultiple()
    {
        fuse f;
        auto r = f.armed([&](fuse&) -> task<> {
            read_source rs(f, 3); // max 3 bytes per read
            rs.provide("abcdefgh");

            char buf[32] = {};

            auto [ec1, n1] = co_await rs.read(make_buffer(buf));
            if(ec1)
                co_return;
            BOOST_TEST_EQ(n1, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abc");

            auto [ec2, n2] = co_await rs.read(make_buffer(buf));
            if(ec2)
                co_return;
            BOOST_TEST_EQ(n2, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "def");

            auto [ec3, n3] = co_await rs.read(make_buffer(buf));
            if(ec3)
                co_return;
            BOOST_TEST_EQ(n3, 2u);
            BOOST_TEST_EQ(std::string_view(buf, n3), "gh");
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testProvide();
        testClear();
        testRead();
        testReadPartial();
        testReadMultiple();
        testReadEof();
        testReadEofAfterData();
        testReadBufferSequence();
        testReadEmpty();
        testFuseErrorInjection();
        testClearAndReuse();
        testMaxReadSize();
        testMaxReadSizeMultiple();
    }
};

TEST_SUITE(read_source_test, "boost.capy.test.read_source");

} // test
} // capy
} // boost
