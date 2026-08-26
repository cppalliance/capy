//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/6.streams/6b.streams.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/write_stream.hpp>
// tag::any_read_stream_include[]
#include <boost/capy/io/any_read_stream.hpp>
// end::any_read_stream_include[]
// tag::any_stream_include[]
#include <boost/capy/io/any_stream.hpp>
// end::any_stream_include[]
// tag::any_write_stream_include[]
#include <boost/capy/io/any_write_stream.hpp>
// end::any_write_stream_include[]
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/write.hpp>

#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {


static_assert(capy::ReadStream<capy::test::stream>);
static_assert(capy::WriteStream<capy::test::stream>);

capy::task<> partial_read(capy::test::stream& stream)
{
    // tag::read_partial[]
    char buf[1024];
    auto [ec, n] = co_await stream.read_some(capy::make_buffer(buf));
    // n might be 1, might be 500, might be 1024
    // if !ec, then n >= 1
    // end::read_partial[]
    BOOST_TEST(! ec);
    BOOST_TEST(n >= 1);
}

// tag::dump_stream[]
template<capy::ReadStream Stream>
capy::task<> dump_stream(Stream& stream)
{
    char buf[256];

    for (;;)
    {
        auto [ec, n] = co_await stream.read_some(capy::make_buffer(buf));

        std::cout.write(buf, n);

        if (ec)
            break;
    }
}
// end::dump_stream[]

capy::task<> partial_write(
    capy::test::stream& stream, std::string const& large_data)
{
    // tag::write_partial[]
    auto [ec, n] = co_await stream.write_some(capy::make_buffer(large_data));
    // n might be less than large_data.size()
    // end::write_partial[]
    BOOST_TEST(! ec);
    BOOST_TEST(n == large_data.size());
}

// The page presents each wrapper's constructor signatures; local class
// scaffolds host the declarations so they compile as shown.
namespace synopsis {

class any_read_stream
{
public:
    // tag::any_read_stream_ctors[]
    // Owning: takes ownership of a moved-in stream
    template<capy::ReadStream S>
    any_read_stream(S stream);

    // Reference: wraps by pointer without ownership
    template<capy::ReadStream S>
    any_read_stream(S* stream);
    // end::any_read_stream_ctors[]
};

class any_write_stream
{
public:
    // tag::any_write_stream_ctors[]
    template<capy::WriteStream S>
    any_write_stream(S stream);   // owning

    template<capy::WriteStream S>
    any_write_stream(S* stream);  // reference
    // end::any_write_stream_ctors[]
};

class any_stream
{
public:
    // tag::any_stream_ctors[]
    template<class S>
        requires capy::ReadStream<S> && capy::WriteStream<S>
    any_stream(S stream);   // owning

    template<class S>
        requires capy::ReadStream<S> && capy::WriteStream<S>
    any_stream(S* stream);  // reference
    // end::any_stream_ctors[]
};

} // namespace synopsis

// Scaffolding target for the wrapper_usage fragment.
void process_stream(capy::any_stream& stream)
{
    capy::test::run_blocking()([](capy::any_stream& s) -> capy::task<>
    {
        auto [ec, n] = co_await s.write_some(capy::const_buffer("ok", 2));
        BOOST_TEST(! ec);
        BOOST_TEST(n == 2);
    }(stream));
}

// tag::echo_server[]
// echo.hpp - Header only declares the signature
capy::task<> handle_connection(capy::any_stream& stream);

// echo.cpp - Implementation in separate translation unit
capy::task<> handle_connection(capy::any_stream& stream)
{
    char buf[1024];

    for (;;)
    {
        auto [ec, n] = co_await stream.read_some(capy::make_buffer(buf));

        auto [wec, wn] = co_await capy::write(
            stream, capy::const_buffer(buf, n));

        if (ec)
            break;

        if (wec)
            break;
    }
}
// end::echo_server[]

struct streams_test
{
    void
    testPartialRead()
    {
        auto [a, b] = capy::test::make_stream_pair();
        b.provide("hello");
        capy::test::run_blocking()(partial_read(a));
    }

    void
    testDumpStream()
    {
        auto [a, b] = capy::test::make_stream_pair();
        b.provide("dumped");
        b.close();

        // Capture std::cout so the fragment's output is observable.
        std::ostringstream out;
        auto* old = std::cout.rdbuf(out.rdbuf());
        capy::test::run_blocking()(dump_stream(a));
        std::cout.rdbuf(old);
        BOOST_TEST(out.str() == "dumped");
    }

    void
    testPartialWrite()
    {
        auto [a, b] = capy::test::make_stream_pair();
        std::string large_data(64, 'x');
        capy::test::run_blocking()(partial_write(a, large_data));
        BOOST_TEST(b.data() == large_data);
    }

    void
    testWrapperUsage()
    {
        // tag::wrapper_usage[]
        void process_stream(capy::any_stream& stream);

        auto [client, server] = capy::test::make_stream_pair();

        // Type erasure, references the existing stream
        capy::any_stream wrapped{&client};
        // process_stream doesn't know about test::stream
        process_stream(wrapped);
        // end::wrapper_usage[]
        BOOST_TEST(server.data() == "ok");
    }

    void
    testEchoServer()
    {
        auto [a, b] = capy::test::make_stream_pair();
        b.provide("echo!");
        b.close();
        capy::any_stream stream{&a};
        capy::test::run_blocking()(handle_connection(stream));
        BOOST_TEST(b.data() == "echo!");
    }

    void
    run()
    {
        testPartialRead();
        testDumpStream();
        testPartialWrite();
        testWrapperUsage();
        testEchoServer();
    }
};

} // namespace

TEST_SUITE(streams_test, "boost.capy.doc.6b_streams");
