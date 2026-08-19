//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/6.streams/6f.isolation.adoc. Pages
// include the tagged regions; scaffolding stays outside the tags. The
// page's protocol.hpp and http_client.hpp live next to this file.


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

// The user_code fragment deliberately omits the headers field in a
// designated initializer; the default is part of the lesson.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#if defined(__clang__) && defined(__has_warning)
#if __has_warning("-Wmissing-designated-field-initializers")
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif
#endif

// tag::protocol_impl[]
// protocol.cpp - Implementation isolated here
#include "protocol.hpp"
#include <boost/capy/read.hpp>
#include <boost/capy/write.hpp>
// end::protocol_impl[]

#include "http_client.hpp"

#include <boost/capy/io/any_read_stream.hpp>
#include <boost/capy/io/any_write_stream.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/test/write_stream.hpp>

#include <string>
#include <utility>

#include "test_suite.hpp"

// tag::protocol_impl[]

task<> handle_protocol(any_stream& stream)
{
    char buf[1024];

    for (;;)
    {
        auto [ec, n] = co_await stream.read_some(make_buffer(buf));
        if (ec)
            co_return;

        // Process and respond...
        std::string response(buf, n);
        co_await write(stream, make_buffer(response));
    }
}
// end::protocol_impl[]

namespace {

// Stand-in transports: real code would bring a network library. Each
// satisfies WriteSink by forwarding to an in-memory test sink; the
// socket additionally satisfies Stream through a loopback pair.
namespace tcp {

class socket
{
    std::pair<test::stream, test::stream> loop_ =
        test::make_stream_pair();
    test::fuse f_;
    test::write_stream sink_{f_};

public:
    auto read_some(MutableBufferSequence auto b)
    {
        return loop_.first.read_some(b);
    }

    auto write_some(ConstBufferSequence auto b)
    {
        return sink_.write_some(b);
    }
};

} // namespace tcp

namespace tls {

class stream
{
    test::fuse f_;
    test::write_stream sink_{f_};

public:
    auto write_some(ConstBufferSequence auto b)
    {
        return sink_.write_some(b);
    }
};

} // namespace tls

struct message
{
    std::string header;
    std::string body;
};

// tag::send_message[]
// Your library code
task<> send_message(any_write_stream& stream, message const& msg)
{
    co_await write(stream, make_buffer(msg.header));
    co_await write(stream, make_buffer(msg.body));
}
// end::send_message[]

task<> use_transports(message const& msg)
{
    {
        // tag::callers[]
        // TCP socket
        tcp::socket socket;
        any_write_stream stream{&socket};  // references socket
        co_await send_message(stream, msg);
        // end::callers[]
    }
    {
        // tag::callers[]

        // TLS stream
        tls::stream tls;
        any_write_stream stream{&tls};  // references tls
        co_await send_message(stream, msg);
        // end::callers[]
    }
    {
        test::fuse f;
        // tag::callers[]

        // Test mock
        test::write_stream mock(f);
        any_write_stream stream{&mock};  // references mock
        co_await send_message(stream, msg);
        // end::callers[]
        BOOST_TEST(mock.data() == msg.header + msg.body);
    }
}

} // namespace

// These namespaces hold the page's declaration-only sketches. They sit
// outside the anonymous namespace so the never-defined declarations do
// not trigger -Wunused-function.
namespace before_after_6f {

// tag::templates_before[]
// Old approach: template propagates everywhere
template<typename Stream>
task<> handle_protocol(Stream& stream);

// Every caller instantiates for their stream type
// Changes force recompilation of all callers
// end::templates_before[]

// tag::type_erasure_after[]
// New approach: concrete signature
task<> handle_protocol(any_stream& stream);

// Implementation compiles once
// Callers only depend on the signature
// end::type_erasure_after[]

} // namespace before_after_6f

namespace guidelines_6f {

// tag::accept_type_erased[]
// Good: accepts any stream
task<> process(any_stream& stream);

// Avoid: forces specific type
task<> process(tcp::socket& socket);
// end::accept_type_erased[]

// Definition so the callers below link.
task<> process(any_stream&)
{
    co_return;
}

// tag::wrap_call_site[]
task<> caller(tcp::socket& socket)
{
    any_stream stream{&socket};  // Wrap by reference here
    co_await process(stream);    // Call with erased type
}
// end::wrap_call_site[]

// tag::return_concrete_decl[]
// OK: factory returns concrete type
tcp::socket create_socket();
// end::return_concrete_decl[]

tcp::socket create_socket()
{
    return {};
}

task<> wrap_if_needed()
{
    // tag::return_concrete_use[]
    // Then caller wraps if needed
    auto socket = create_socket();
    any_stream stream{&socket};  // reference; socket must outlive stream
    // or: any_stream stream{std::move(socket)};  // wrapper takes ownership
    // end::return_concrete_use[]
    co_await process(stream);
}

} // namespace guidelines_6f

namespace {

// The request literal intentionally leaves `headers` defaulted.
task<> user_code()
{
    // tag::user_code[]
    // User code
    tcp::socket socket;
    // ... connect ...

    any_stream conn{&socket};  // references socket
    http_request req{
        .method = "GET",
        .url = "/api/data"
    };
    auto response = co_await send_request(conn, req);

    // Read body through type-erased source
    char storage[4096];
    mutable_buffer buf(storage, sizeof(storage));
    auto [ec, n] = co_await response.body.read_some(buf);
    // end::user_code[]
    BOOST_TEST(!ec);
    BOOST_TEST(response.status_code == 200);
    BOOST_TEST(std::string_view(storage, n) == "{}");
}

struct isolation_test
{
    void
    testHandleProtocol()
    {
        auto [a, b] = test::make_stream_pair();
        b.provide("ping");
        b.close();
        any_stream stream{&a};
        test::run_blocking()(handle_protocol(stream));
        // The protocol echoes what it read back to the peer.
        BOOST_TEST(b.data() == "ping");
    }

    void
    testSendMessage()
    {
        test::fuse f;
        test::write_stream mock(f);
        any_write_stream stream{&mock};
        test::run_blocking()(
            send_message(stream, {"HDR", "BODY"}));
        BOOST_TEST(mock.data() == "HDRBODY");
    }

    void
    testTransports()
    {
        test::run_blocking()(
            use_transports({"HDR", "BODY"}));
    }

    void
    testGuidelines()
    {
        tcp::socket socket;
        test::run_blocking()(guidelines_6f::caller(socket));
        test::run_blocking()(guidelines_6f::wrap_if_needed());
    }

    void
    testUserCode()
    {
        test::run_blocking()(user_code());
    }

    void
    run()
    {
        testHandleProtocol();
        testSendMessage();
        testTransports();
        testGuidelines();
        testUserCode();
    }
};

} // namespace

// Definition so the user-code fragment links; a real client would parse
// an HTTP response off the wire.
task<http_response> send_request(any_stream&, http_request const&)
{
    test::fuse f;
    test::read_stream body(f);
    body.provide("{}");
    co_return http_response{200, {}, any_read_stream(std::move(body))};
}

TEST_SUITE(isolation_test, "boost.capy.doc.6f_isolation");
