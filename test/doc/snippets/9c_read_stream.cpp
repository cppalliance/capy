//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/9.design/9c.ReadStream.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/mutable_buffer_sequence.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/read.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/write_stream.hpp>

#include <cstddef>
#include <string_view>
#include <system_error>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {


static_assert(capy::ReadStream<capy::test::read_stream>);

namespace composed {

// tag::read_signature[]
auto read(capy::ReadStream auto& stream,
          capy::MutableBufferSequence auto buffers)
    -> capy::io_task<std::size_t>;
// end::read_signature[]

} // namespace composed

// tag::read_header[]
template<capy::ReadStream Stream>
capy::task<> read_header(Stream& stream)
{
    char header[16];
    auto [ec, n] = co_await capy::read(
        stream, capy::make_buffer(header));
    if(ec == capy::cond::eof)
        co_return;  // clean shutdown
    if(ec)
        co_return;
    // header contains exactly 16 bytes
}
// end::read_header[]

// tag::echo[]
template<capy::ReadStream Stream>
capy::task<> echo(Stream& stream, capy::WriteStream auto& dest)
{
    char buf[4096];
    for(;;)
    {
        auto [ec, n] = co_await stream.read_some(
            capy::make_buffer(buf));

        auto [wec, nw] = co_await dest.write_some(
            capy::const_buffer(buf, n));

        if(ec)
            co_return;

        if(wec)
            co_return;
    }
}
// end::echo[]

// Scaffold for the canonical advance-then-check loop.
template<capy::ReadStream S>
capy::task<> read_all(S& s, char* buf, std::size_t size)
{
    std::size_t total = 0;
    while(total < size)
    {
        // tag::canonical_loop[]
        auto [ec, n] = co_await s.read_some(
            capy::mutable_buffer(buf + total, size - total));
        total += n;
        if(ec)
            co_return;
        // end::canonical_loop[]
    }
}

struct read_stream_test
{
    void
    testReadHeader()
    {
        capy::test::read_stream rs;
        rs.provide("0123456789abcdef");
        capy::test::run_blocking()(read_header(rs));
        BOOST_TEST(rs.available() == 0);
    }

    void
    testEcho()
    {
        capy::test::read_stream rs;
        capy::test::write_stream ws;
        rs.provide("hello");
        capy::test::run_blocking()(echo(rs, ws));
        BOOST_TEST(ws.data() == "hello");
    }

    void
    testCanonicalLoop()
    {
        capy::test::read_stream rs;
        rs.provide("abcdefgh");
        char buf[8];
        capy::test::run_blocking()(read_all(rs, buf, sizeof(buf)));
        BOOST_TEST(std::string_view(buf, 8) == "abcdefgh");
    }

    void
    run()
    {
        testReadHeader();
        testEcho();
        testCanonicalLoop();
    }
};

} // namespace

TEST_SUITE(read_stream_test, "boost.capy.doc.9c_read_stream");
