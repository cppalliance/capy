//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/6.streams/6b.streams.adoc.

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
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/const_buffer_sequence.hpp>
#include <boost/capy/concept/mutable_buffer_sequence.hpp>
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

using namespace boost::capy;

namespace definition {

// tag::read_stream_concept[]
template<typename T>
concept ReadStream =
    requires(T& stream, mutable_buffer_archetype buffers)
    {
        { stream.read_some(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(stream.read_some(buffers)),
            std::error_code, std::size_t>;
    };
// end::read_stream_concept[]

// tag::write_stream_concept[]
template<typename T>
concept WriteStream =
    requires(T& stream, const_buffer_archetype buffers)
    {
        { stream.write_some(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(stream.write_some(buffers)),
            std::error_code, std::size_t>;
    };
// end::write_stream_concept[]

// The page's definitions must match the library's.
static_assert(definition::ReadStream<capy::test::stream>);
static_assert(capy::ReadStream<capy::test::stream>);
static_assert(definition::WriteStream<capy::test::stream>);
static_assert(capy::WriteStream<capy::test::stream>);

} // namespace definition

// Scaffolds owning the conforming-signature declarations.
struct conforming_read_stream
{
    // tag::read_some_signature[]
    template<MutableBufferSequence Buffers>
    IoAwaitable auto read_some(Buffers buffers);
    // end::read_some_signature[]
};

struct conforming_write_stream
{
    // tag::write_some_signature[]
    template<ConstBufferSequence Buffers>
    IoAwaitable auto write_some(Buffers buffers);
    // end::write_some_signature[]
};

task<> partial_read(test::stream& stream)
{
    // tag::read_partial[]
    char buf[1024];
    auto [ec, n] = co_await stream.read_some(make_buffer(buf));
    // n might be 1, might be 500, might be 1024
    // if !ec, then n >= 1
    // end::read_partial[]
    BOOST_TEST(! ec);
    BOOST_TEST(n >= 1);
}

// tag::dump_stream[]
template<ReadStream Stream>
task<> dump_stream(Stream& stream)
{
    char buf[256];

    for (;;)
    {
        auto [ec, n] = co_await stream.read_some(make_buffer(buf));

        std::cout.write(buf, n);

        if (ec)
            break;
    }
}
// end::dump_stream[]

task<> partial_write(test::stream& stream, std::string const& large_data)
{
    // tag::write_partial[]
    auto [ec, n] = co_await stream.write_some(make_buffer(large_data));
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
    template<ReadStream S>
    any_read_stream(S stream);

    // Reference: wraps by pointer without ownership
    template<ReadStream S>
    any_read_stream(S* stream);
    // end::any_read_stream_ctors[]
};

class any_write_stream
{
public:
    // tag::any_write_stream_ctors[]
    template<WriteStream S>
    any_write_stream(S stream);   // owning

    template<WriteStream S>
    any_write_stream(S* stream);  // reference
    // end::any_write_stream_ctors[]
};

class any_stream
{
public:
    // tag::any_stream_ctors[]
    template<class S>
        requires ReadStream<S> && WriteStream<S>
    any_stream(S stream);   // owning

    template<class S>
        requires ReadStream<S> && WriteStream<S>
    any_stream(S* stream);  // reference
    // end::any_stream_ctors[]
};

} // namespace synopsis

// Scaffolding target for the wrapper_usage fragment.
void process_stream(any_stream& stream)
{
    test::run_blocking()([](any_stream& s) -> task<>
    {
        auto [ec, n] = co_await s.write_some(const_buffer("ok", 2));
        BOOST_TEST(! ec);
        BOOST_TEST(n == 2);
    }(stream));
}

// tag::echo_server[]
// echo.hpp - Header only declares the signature
task<> handle_connection(any_stream& stream);

// echo.cpp - Implementation in separate translation unit
task<> handle_connection(any_stream& stream)
{
    char buf[1024];

    for (;;)
    {
        auto [ec, n] = co_await stream.read_some(make_buffer(buf));

        auto [wec, wn] = co_await write(stream, const_buffer(buf, n));

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
        auto [a, b] = test::make_stream_pair();
        b.provide("hello");
        test::run_blocking()(partial_read(a));
    }

    void
    testDumpStream()
    {
        auto [a, b] = test::make_stream_pair();
        b.provide("dumped");
        b.close();

        // Capture std::cout so the fragment's output is observable.
        std::ostringstream out;
        auto* old = std::cout.rdbuf(out.rdbuf());
        test::run_blocking()(dump_stream(a));
        std::cout.rdbuf(old);
        BOOST_TEST(out.str() == "dumped");
    }

    void
    testPartialWrite()
    {
        auto [a, b] = test::make_stream_pair();
        std::string large_data(64, 'x');
        test::run_blocking()(partial_write(a, large_data));
        BOOST_TEST(b.data() == large_data);
    }

    void
    testWrapperUsage()
    {
        // tag::wrapper_usage[]
        void process_stream(any_stream& stream);

        auto [client, server] = test::make_stream_pair();

        any_stream wrapped{&client};  // Type erasure, references the existing stream
        process_stream(wrapped);      // process_stream doesn't know about test::stream
        // end::wrapper_usage[]
        BOOST_TEST(server.data() == "ok");
    }

    void
    testEchoServer()
    {
        auto [a, b] = test::make_stream_pair();
        b.provide("echo!");
        b.close();
        any_stream stream{&a};
        test::run_blocking()(handle_connection(stream));
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
