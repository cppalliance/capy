//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/7.testing/7b.mock-streams.adoc.

// Fragments deliberately leave results and bindings unused; the pages
// explain the values in prose instead.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-function"
// gcc 15 with sanitizers misattributes coroutine frame delete paths
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
#if defined(_MSC_VER)
#pragma warning(disable: 4834) // discarding [[nodiscard]] return value
#pragma warning(disable: 4189) // local variable initialized but not referenced
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4101) // unreferenced local variable
#pragma warning(disable: 4456) // declaration hides previous local declaration
#pragma warning(disable: 4457) // declaration hides function parameter
#pragma warning(disable: 4458) // declaration hides class member
#pragma warning(disable: 4459) // declaration hides global declaration
#endif

// GCC gives false positive -Wmaybe-uninitialized on structured bindings
// via the tuple protocol inside coroutine frames.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/test/write_stream.hpp>

#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "test_suite.hpp"

namespace {

// The #include directives inside the tags expand to nothing here (the
// headers are already included above); they are kept for the page text.

// tag::read_stream_basic[]
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>

using namespace boost::capy;
using namespace boost::capy::test;

void test_read_stream()
{
    fuse f;
    read_stream rs(f);
    rs.provide("Hello, ");
    rs.provide("World!");

    auto r = f.armed([&](fuse&) -> task<void> {
        char buf[32];
        auto [ec, n] = co_await rs.read_some(
            mutable_buffer(buf, sizeof(buf)));
        if(ec)
            co_return;
        BOOST_TEST(std::string_view(buf, n) == "Hello, World!");
    });
    BOOST_TEST(r.success);
}
// end::read_stream_basic[]

// tag::write_stream_basic[]
#include <boost/capy/test/write_stream.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>

using namespace boost::capy;
using namespace boost::capy::test;

void test_write_stream()
{
    fuse f;

    auto r = f.armed([&](fuse&) -> task<void> {
        write_stream ws(f);

        auto [ec, n] = co_await ws.write_some(
            const_buffer("Hello", 5));
        if(ec)
            co_return;
        BOOST_TEST(ws.data() == "Hello");
    });
    BOOST_TEST(r.success);
}
// end::write_stream_basic[]

// tag::stream_pair_basic[]
#include <boost/capy/test/stream.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/task.hpp>

using namespace boost::capy;
using namespace boost::capy::test;

void test_stream_pair()
{
    fuse f;

    auto r = f.armed([&](fuse&) -> task<void> {
        auto [a, b] = make_stream_pair(f);

        auto [ec, n] = co_await a.write_some(
            const_buffer("hello", 5));
        if(ec)
            co_return;

        char buf[32];
        auto [ec2, n2] = co_await b.read_some(
            mutable_buffer(buf, sizeof(buf)));
        if(ec2)
            co_return;
        BOOST_TEST(std::string_view(buf, n2) == "hello");
    });
    BOOST_TEST(r.success);
}
// end::stream_pair_basic[]

// tag::read_line_test[]
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>

using namespace boost::capy;
using namespace boost::capy::test;

// Function under test: read until '\n' or EOF
template<ReadStream S>
task<std::pair<std::error_code, std::string>>
read_line(S& stream)
{
    std::string line;
    char ch;
    for(;;)
    {
        auto [ec, n] = co_await stream.read_some(
            mutable_buffer(&ch, 1));
        if(ec)
            co_return {ec, std::move(line)};
        if(ch == '\n')
            break;
        line += ch;
    }
    co_return {std::error_code{}, std::move(line)};
}

void test_read_line()
{
    fuse f;
    auto r = f.armed([&](fuse&) -> task<void> {
        read_stream rs(f);
        rs.provide("hello\n");

        auto [ec, line] = co_await read_line(rs);
        if(ec)
            co_return;  // fuse injected an error; exit gracefully
        BOOST_TEST(line == "hello");
    });
    BOOST_TEST(r.success);
}
// end::read_line_test[]

struct mock_streams_test
{
    void
    testReadStreamBasic()
    {
        test_read_stream();
    }

    void
    testReadStreamChunked()
    {
        // tag::read_stream_chunked[]
        // At most 4 bytes per read_some call
        fuse f;
        read_stream rs(f, 4);
        rs.provide("Hello, World!");

        auto r = f.armed([&](fuse&) -> task<void> {
            char buf[32];
            auto [ec, n] = co_await rs.read_some(
                mutable_buffer(buf, sizeof(buf)));
            if(ec)
                co_return;
            BOOST_TEST(n == 4);  // "Hell"
        });
        BOOST_TEST(r.success);
        // end::read_stream_chunked[]
    }

    void
    testReadStreamEof()
    {
        // tag::read_stream_eof[]
        fuse f;
        read_stream rs(f);
        rs.provide("hi");

        auto r = f.inert([&](fuse&) -> task<void> {
            char buf[8];
            // First read: consumes "hi"
            auto [ec, n] = co_await rs.read_some(
                mutable_buffer(buf, sizeof(buf)));
            BOOST_TEST(!ec);
            BOOST_TEST(std::string_view(buf, n) == "hi");

            // Second read: EOF
            auto [ec2, n2] = co_await rs.read_some(
                mutable_buffer(buf, sizeof(buf)));
            BOOST_TEST(ec2 == cond::eof);
            BOOST_TEST(n2 == 0);
        });
        BOOST_TEST(r.success);
        // end::read_stream_eof[]
    }

    void
    testWriteStreamBasic()
    {
        test_write_stream();
    }

    void
    testWriteStreamChunked()
    {
        // tag::write_stream_chunked[]
        fuse f;
        write_stream ws(f, 4);  // accept at most 4 bytes per call

        auto r = f.inert([&](fuse&) -> task<void> {
            auto [ec, n] = co_await ws.write_some(
                const_buffer("Hello", 5));
            BOOST_TEST(!ec);
            BOOST_TEST(n == 4);  // only "Hell" was accepted
        });
        BOOST_TEST(r.success);
        // end::write_stream_chunked[]
    }

    void
    testWriteStreamExpect()
    {
        // tag::write_stream_expect[]
        fuse f;
        write_stream ws(f);
        ws.expect("Hello World");

        auto r = f.inert([&](fuse&) -> task<void> {
            // Writing matching data succeeds
            auto [ec, n] = co_await ws.write_some(
                const_buffer("Hello World", 11));
            BOOST_TEST(!ec);
        });
        BOOST_TEST(r.success);
        // end::write_stream_expect[]
    }

    void
    testStreamPairBasic()
    {
        test_stream_pair();
    }

    void
    testReadLine()
    {
        test_read_line();
    }

    void
    run()
    {
        testReadStreamBasic();
        testReadStreamChunked();
        testReadStreamEof();
        testWriteStreamBasic();
        testWriteStreamChunked();
        testWriteStreamExpect();
        testStreamPairBasic();
        testReadLine();
    }
};

} // namespace

TEST_SUITE(mock_streams_test, "boost.capy.doc.7b_mock_streams");
