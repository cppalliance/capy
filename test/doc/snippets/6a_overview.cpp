//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/6.streams/6a.overview.adoc.

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

using namespace boost::capy;

task<> partial_read(test::stream& stream)
{
    char storage[1024];
    auto buffer = make_buffer(storage);
    // tag::read_stream_partial[]
    // ReadStream: may return fewer bytes than buffer can hold
    auto [ec, n] = co_await stream.read_some(buffer);
    // n might be 1, might be 1000, might be buffer_size(buffer)
    // end::read_stream_partial[]
    BOOST_TEST(! ec);
    BOOST_TEST(n >= 1);
}

task<> partial_write(test::stream& stream)
{
    const_buffer buffers("hello", 5);
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
task<> echo(any_stream& stream)
{
    char buf[1024];
    for (;;)
    {
        auto [ec, n] = co_await stream.read_some(make_buffer(buf));

        auto [wec, wn] = co_await write(stream, const_buffer(buf, n));

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
        auto [a, b] = test::make_stream_pair();
        b.provide("hello");
        test::run_blocking()(partial_read(a));
        test::run_blocking()(partial_write(a));
        BOOST_TEST(b.data() == "hello");
    }

    void
    testCallerDecides()
    {
        // tag::caller_decides[]
        auto [client, server] = test::make_stream_pair();

        // Owns the moved-in stream (the by-value form takes ownership)
        any_stream s1{std::move(client)};

        // Wraps the server end by pointer (reference semantics, must outlive s2)
        any_stream s2{&server};
        // end::caller_decides[]

        // Feed the client end and signal eof so echo() terminates.
        server.provide("ping");
        server.close();

        // tag::caller_decides[]

        // Same echo() coroutine regardless of how the wrapper was built
        test::run_blocking()(echo(s1));
        // end::caller_decides[]

        // The echoed bytes arrive on the server end; read them
        // through s2 to exercise the reference-mode wrapper.
        test::run_blocking()([](any_stream& s) -> task<>
        {
            char buf[8];
            auto [ec, n] = co_await s.read_some(make_buffer(buf));
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
