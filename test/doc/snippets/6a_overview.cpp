//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/6.streams/6a.overview.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/write.hpp>

#include <cstddef>
#include <string_view>
#include <utility>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {


capy::task<> partial_read(capy::test::stream& stream)
{
    char storage[1024];
    auto buffer = capy::make_buffer(storage);
    // tag::read_stream_partial[]
    // ReadStream: may return fewer bytes than buffer can hold
    auto [ec, n] = co_await stream.read_some(buffer);
    // n might be 1, might be 1000, might be buffer_size(buffer)
    // end::read_stream_partial[]
    BOOST_TEST(! ec);
    BOOST_TEST(n >= 1);
}

capy::task<> partial_write(capy::test::stream& stream)
{
    capy::const_buffer buffers("hello", 5);
    // tag::write_stream_partial[]
    // WriteStream: may write fewer bytes than provided
    auto [ec, n] = co_await stream.write_some(buffers);
    // n might be less than buffer_size(buffers)
    // end::write_stream_partial[]
    BOOST_TEST(! ec);
    BOOST_TEST(n == 5);
}

// tag::any_stream_echo[]
// This function works with any stream implementation
capy::task<> echo(capy::any_stream& stream)
{
    char buf[1024];
    for (;;)
    {
        auto [ec, n] = co_await stream.read_some(capy::make_buffer(buf));

        auto [wec, wn] = co_await capy::write(
            stream, capy::const_buffer(buf, n));

        if (ec)
            co_return;

        if (wec)
            co_return;
    }
}
// end::any_stream_echo[]

struct overview_test
{
    void
    testPartialReadWrite()
    {
        auto [a, b] = capy::test::make_stream_pair();
        b.provide("hello");
        capy::test::run_blocking()(partial_read(a));
        capy::test::run_blocking()(partial_write(a));
        BOOST_TEST(b.data() == "hello");
    }

    void
    testCallerDecides()
    {
        // tag::caller_decides[]
        auto [client, server] = capy::test::make_stream_pair();

        // Owns the moved-in stream (the by-value form takes ownership)
        capy::any_stream s1{std::move(client)};

        // Wraps the server end by pointer (reference semantics, must outlive s2)
        capy::any_stream s2{&server};
        // end::caller_decides[]

        // Feed the client end and signal eof so echo() terminates.
        server.provide("ping");
        server.close();

        // tag::caller_decides[]

        // Same echo() coroutine regardless of how the wrapper was built
        capy::test::run_blocking()(echo(s1));
        // end::caller_decides[]

        // The echoed bytes arrive on the server end; read them
        // through s2 to exercise the reference-mode wrapper.
        capy::test::run_blocking()([](capy::any_stream& s) -> capy::task<>
        {
            char buf[8];
            auto [ec, n] = co_await s.read_some(capy::make_buffer(buf));
            BOOST_TEST(! ec);
            BOOST_TEST(std::string_view(buf, n) == "ping");
        }(s2));
    }

    void
    run()
    {
        testPartialReadWrite();
        testCallerDecides();
    }
};

} // namespace

TEST_SUITE(overview_test, "boost.capy.doc.6a_overview");
