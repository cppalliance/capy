//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/pull_from.hpp>

#include <boost/capy/test/buffer_sink.hpp>
#include <boost/capy/test/read_source.hpp>
#include <boost/capy/test/read_stream.hpp>

#include "test/unit/test_helpers.hpp"

#include <string_view>

namespace boost {
namespace capy {
namespace {

class pull_from_test
{
public:
    //-------------------------------------------------------------------
    // ReadSource → BufferSink tests
    //-------------------------------------------------------------------

    void
    testReadSourceToBufferSinkEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            // ReadSource returns error::eof when empty, but pull_from handles it
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(bs.data().empty());
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSourceToBufferSinkSingle()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hello world");
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(bs.data(), "hello world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSourceToBufferSinkMultiple()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hello");
            rs.provide(" ");
            rs.provide("world");
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(bs.data(), "hello world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSourceToBufferSinkPartialRead()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f, 5); // max 5 bytes per read
            rs.provide("hello world");
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(bs.data(), "hello world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSourceToBufferSinkLarge()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            std::string large_data(10000, 'x');
            rs.provide(large_data);
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 10000u);
            BOOST_TEST_EQ(bs.size(), 10000u);
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSourceToBufferSinkSmallSinkBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("hello world");
            test::buffer_sink bs(f, 5); // small buffer

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(bs.data(), "hello world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadSourceToBufferSinkSourceError()
    {
        int error_count = 0;
        int success_count = 0;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("test data");
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
            {
                ++error_count;
                co_return;
            }
            ++success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(error_count > 0);
        BOOST_TEST(success_count > 0);
    }

    void
    testReadSourceToBufferSinkSinkError()
    {
        int error_count = 0;
        int success_count = 0;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_source rs(f);
            rs.provide("test data");
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
            {
                ++error_count;
                co_return;
            }
            ++success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(error_count > 0);
        BOOST_TEST(success_count > 0);
    }

    //-------------------------------------------------------------------
    // ReadStream → BufferSink tests
    //-------------------------------------------------------------------

    void
    testReadStreamToBufferSinkEmpty()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 0u);
            BOOST_TEST(bs.data().empty());
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadStreamToBufferSinkSingle()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("hello world");
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(bs.data(), "hello world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadStreamToBufferSinkMultiple()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("hello");
            rs.provide(" ");
            rs.provide("world");
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(bs.data(), "hello world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadStreamToBufferSinkPartialRead()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f, 3); // max 3 bytes per read
            rs.provide("hello world");
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(bs.data(), "hello world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadStreamToBufferSinkLarge()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            std::string large_data(10000, 'y');
            rs.provide(large_data);
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 10000u);
            BOOST_TEST_EQ(bs.size(), 10000u);
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadStreamToBufferSinkSmallSinkBuffer()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("hello world");
            test::buffer_sink bs(f, 4); // small buffer

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 11u);
            BOOST_TEST_EQ(bs.data(), "hello world");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadStreamToBufferSinkChunked()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f, 7); // max 7 bytes per read
            rs.provide("hello world test data");
            test::buffer_sink bs(f, 5); // max 5 bytes per prepare

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(n, 21u);
            BOOST_TEST_EQ(bs.data(), "hello world test data");
            BOOST_TEST(bs.eof_called());
        });
        BOOST_TEST(r.success);
    }

    void
    testReadStreamToBufferSinkStreamError()
    {
        int error_count = 0;
        int success_count = 0;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("test data");
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
            {
                ++error_count;
                co_return;
            }
            ++success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(error_count > 0);
        BOOST_TEST(success_count > 0);
    }

    void
    testReadStreamToBufferSinkSinkError()
    {
        int error_count = 0;
        int success_count = 0;

        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            test::read_stream rs(f);
            rs.provide("test data");
            test::buffer_sink bs(f);

            auto [ec, n] = co_await pull_from(rs, bs);
            if(ec.failed())
            {
                ++error_count;
                co_return;
            }
            ++success_count;
        });

        BOOST_TEST(r.success);
        BOOST_TEST(error_count > 0);
        BOOST_TEST(success_count > 0);
    }

    void
    run()
    {
        // ReadSource → BufferSink tests
        testReadSourceToBufferSinkEmpty();
        testReadSourceToBufferSinkSingle();
        testReadSourceToBufferSinkMultiple();
        testReadSourceToBufferSinkPartialRead();
        testReadSourceToBufferSinkLarge();
        testReadSourceToBufferSinkSmallSinkBuffer();
        testReadSourceToBufferSinkSourceError();
        testReadSourceToBufferSinkSinkError();

        // ReadStream → BufferSink tests
        testReadStreamToBufferSinkEmpty();
        testReadStreamToBufferSinkSingle();
        testReadStreamToBufferSinkMultiple();
        testReadStreamToBufferSinkPartialRead();
        testReadStreamToBufferSinkLarge();
        testReadStreamToBufferSinkSmallSinkBuffer();
        testReadStreamToBufferSinkChunked();
        testReadStreamToBufferSinkStreamError();
        testReadStreamToBufferSinkSinkError();
    }
};

TEST_SUITE(pull_from_test, "boost.capy.io.pull_from");

} // namespace
} // capy
} // boost
