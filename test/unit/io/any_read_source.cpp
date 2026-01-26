//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/any_read_source.hpp>

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/read_source.hpp>

#include "test/unit/test_helpers.hpp"

#include <array>
#include <string_view>

namespace boost {
namespace capy {
namespace {

class any_read_source_test
{
public:
    void
    testConstruct()
    {
        // Default construct
        {
            any_read_source ars;
            BOOST_TEST(!ars.has_value());
            BOOST_TEST(!ars);
        }

        // Construct from source
        {
            test::fuse f;
            test::read_source rs(f);
            any_read_source ars(rs);
            BOOST_TEST(ars.has_value());
            BOOST_TEST(static_cast<bool>(ars));
        }
    }

    void
    testMove()
    {
        test::fuse f;
        test::read_source rs(f);

        any_read_source ars1(rs);
        BOOST_TEST(ars1.has_value());

        // Move construct
        any_read_source ars2(std::move(ars1));
        BOOST_TEST(ars2.has_value());
        BOOST_TEST(!ars1.has_value());

        // Move assign
        any_read_source ars3;
        ars3 = std::move(ars2);
        BOOST_TEST(ars3.has_value());
        BOOST_TEST(!ars2.has_value());
    }

    void
    testRead()
    {
        // Buffer exactly matches available data
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hello world");

            any_read_source ars(rs);

            char buf[11] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf, 11));
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadPartial()
    {
        // Buffer smaller than available data - fills completely
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hello world");

            any_read_source ars(rs);

            char buf[5] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf));
            if(ec.failed())
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
        // Multiple reads that exactly consume available data
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("abcdefghi");

            any_read_source ars(rs);

            char buf[3] = {};

            auto [ec1, n1] = co_await ars.read(make_buffer(buf));
            if(ec1.failed())
                co_return;
            BOOST_TEST_EQ(n1, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abc");

            auto [ec2, n2] = co_await ars.read(make_buffer(buf));
            if(ec2.failed())
                co_return;
            BOOST_TEST_EQ(n2, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "def");

            auto [ec3, n3] = co_await ars.read(make_buffer(buf));
            if(ec3.failed())
                co_return;
            BOOST_TEST_EQ(n3, 3u);
            BOOST_TEST_EQ(std::string_view(buf, n3), "ghi");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadInsufficientData()
    {
        // Buffer larger than available data - fails with EOF
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hi");

            any_read_source ars(rs);

            char buf[10] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf));
            // Should fail because buffer can't be filled
            if(ec.failed() && ec != cond::eof)
                co_return; // fuse-injected error
            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 2u); // 2 bytes read before EOF
        });
        BOOST_TEST(r.success);
    }

    void
    testReadEof()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            // No data provided - should get EOF

            any_read_source ars(rs);

            char buf[32] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf));
            if(ec.failed() && ec != cond::eof)
                co_return;

            BOOST_TEST(ec == cond::eof);
            BOOST_TEST_EQ(n, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadEofAfterData()
    {
        // Read exact amount, then get EOF on next read
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("x");

            any_read_source ars(rs);

            char buf[1] = {};

            auto [ec1, n1] = co_await ars.read(make_buffer(buf));
            if(ec1.failed())
                co_return;
            BOOST_TEST_EQ(n1, 1u);

            auto [ec2, n2] = co_await ars.read(make_buffer(buf));
            // Should get EOF because no more data
            if(ec2.failed() && ec2 != cond::eof)
                co_return; // fuse-injected error
            BOOST_TEST(ec2 == cond::eof);
            BOOST_TEST_EQ(n2, 0u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadBufferSequence()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("helloworld");

            any_read_source ars(rs);

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                make_buffer(buf1),
                make_buffer(buf2)
            }};

            auto [ec, n] = co_await ars.read(buffers);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 10u);
            BOOST_TEST_EQ(std::string_view(buf1, 5), "hello");
            BOOST_TEST_EQ(std::string_view(buf2, 5), "world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSingleBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hello world");

            any_read_source ars(rs);

            char buf[11] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf));
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadArray()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("helloworld");

            any_read_source ars(rs);

            char buf1[5] = {};
            char buf2[5] = {};
            std::array<mutable_buffer, 2> buffers = {{
                make_buffer(buf1),
                make_buffer(buf2)
            }};

            auto [ec, n] = co_await ars.read(buffers);
            if(ec.failed())
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
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("data");

            any_read_source ars(rs);

            auto [ec, n] = co_await ars.read(mutable_buffer());
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST_EQ(rs.available(), 4u);
        });
        BOOST_TEST(r.success);
    }

    void
    testReadWithMaxReadSize()
    {
        // Verify any_read_source loops to fill buffer even when
        // underlying source has max_read_size limitation
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f, 5); // max 5 bytes per read
            rs.provide("hello world");

            any_read_source ars(rs);

            char buf[11] = {};
            auto [ec, n] = co_await ars.read(make_buffer(buf));
            if(ec.failed())
                co_return;

            // Should fill entire buffer by looping
            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(std::string_view(buf, n), "hello world");
        });
        BOOST_TEST(r.success);
    }

    void
    testReadWithMaxReadSizeMultiple()
    {
        // Verify multiple reads with max_read_size, each filling buffer
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f, 3); // max 3 bytes per read
            rs.provide("abcdefghij");

            any_read_source ars(rs);

            char buf[5] = {};

            // First read: fills 5 bytes by looping (3 + 2)
            auto [ec1, n1] = co_await ars.read(make_buffer(buf));
            if(ec1.failed())
                co_return;
            BOOST_TEST_EQ(n1, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n1), "abcde");

            // Second read: fills 5 bytes by looping (3 + 2)
            auto [ec2, n2] = co_await ars.read(make_buffer(buf));
            if(ec2.failed())
                co_return;
            BOOST_TEST_EQ(n2, 5u);
            BOOST_TEST_EQ(std::string_view(buf, n2), "fghij");
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testMove();
        testRead();
        testReadPartial();
        testReadMultiple();
        testReadInsufficientData();
        testReadEof();
        testReadEofAfterData();
        testReadBufferSequence();
        testReadSingleBuffer();
        testReadArray();
        testReadEmpty();
        testReadWithMaxReadSize();
        testReadWithMaxReadSizeMultiple();
    }
};

TEST_SUITE(any_read_source_test, "boost.capy.io.any_read_source");

} // namespace
} // namespace capy
} // namespace boost
