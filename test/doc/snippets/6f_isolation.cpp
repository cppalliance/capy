//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/6.streams/6f.isolation.adoc. Pages
// include the tagged regions; scaffolding stays outside the tags. The
// page's protocol.hpp and http_client.hpp live next to this file.


#include "../doc_warnings.hpp"

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

namespace capy = boost::capy;

// tag::protocol_impl[]

capy::task<> handle_protocol(capy::any_stream& stream)
{
    char buf[1024];

    for (;;)
    {
        auto [ec, n] = co_await stream.read_some(capy::make_buffer(buf));
        if (ec)
            co_return;

        // Process and respond...
        std::string response(buf, n);
        co_await capy::write(stream, capy::make_buffer(response));
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
    std::pair<capy::test::stream, capy::test::stream> loop_ =
        capy::test::make_stream_pair();
    capy::test::fuse f_;
    capy::test::write_stream sink_{f_};

public:
    auto read_some(capy::MutableBufferSequence auto b)
    {
        return loop_.first.read_some(b);
    }

    auto write_some(capy::ConstBufferSequence auto b)
    {
        return sink_.write_some(b);
    }
};

} // namespace tcp

namespace tls {

class stream
{
    capy::test::fuse f_;
    capy::test::write_stream sink_{f_};

public:
    auto write_some(capy::ConstBufferSequence auto b)
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
capy::task<> send_message(capy::any_write_stream& stream, message const& msg)
{
    co_await capy::write(stream, capy::make_buffer(msg.header));
    co_await capy::write(stream, capy::make_buffer(msg.body));
}
// end::send_message[]

capy::task<> use_transports(message const& msg)
{
    {
        // tag::callers[]
        // TCP socket
        tcp::socket socket;
        capy::any_write_stream stream{&socket};  // references socket
        co_await send_message(stream, msg);
        // end::callers[]
    }
    {
        // tag::callers[]

        // TLS stream
        tls::stream tls;
        capy::any_write_stream stream{&tls};  // references tls
        co_await send_message(stream, msg);
        // end::callers[]
    }
    {
        capy::test::fuse f;
        // tag::callers[]

        // Test mock
        capy::test::write_stream mock(f);
        capy::any_write_stream stream{&mock};  // references mock
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
capy::task<> handle_protocol(Stream& stream);

// Every caller instantiates for their stream type
// Changes force recompilation of all callers
// end::templates_before[]

// tag::type_erasure_after[]
// New approach: concrete signature
capy::task<> handle_protocol(capy::any_stream& stream);

// Implementation compiles once
// Callers only depend on the signature
// end::type_erasure_after[]

} // namespace before_after_6f

namespace guidelines_6f {

// tag::accept_type_erased[]
// Good: accepts any stream
capy::task<> process(capy::any_stream& stream);

// Avoid: forces specific type
capy::task<> process(tcp::socket& socket);
// end::accept_type_erased[]

// Definition so the callers below link.
capy::task<> process(capy::any_stream&)
{
    co_return;
}

// tag::wrap_call_site[]
capy::task<> caller(tcp::socket& socket)
{
    capy::any_stream stream{&socket};  // Wrap by reference here
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

capy::task<> wrap_if_needed()
{
    // tag::return_concrete_use[]
    // Then caller wraps if needed
    auto socket = create_socket();
    capy::any_stream stream{&socket};  // reference; socket must outlive stream
    // or: any_stream stream{std::move(socket)};  // wrapper takes ownership
    // end::return_concrete_use[]
    co_await process(stream);
}

} // namespace guidelines_6f

namespace {

// The request literal intentionally leaves `headers` defaulted.
capy::task<> user_code()
{
    // tag::user_code[]
    // User code
    tcp::socket socket;
    // ... connect ...

    capy::any_stream conn{&socket};  // references socket
    http_request req{
        .method = "GET",
        .url = "/api/data"
    };
    auto response = co_await send_request(conn, req);

    // Read body through type-erased source
    char storage[4096];
    capy::mutable_buffer buf(storage, sizeof(storage));
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
        auto [a, b] = capy::test::make_stream_pair();
        b.provide("ping");
        b.close();
        capy::any_stream stream{&a};
        capy::test::run_blocking()(handle_protocol(stream));
        // The protocol echoes what it read back to the peer.
        BOOST_TEST(b.data() == "ping");
    }

    void
    testSendMessage()
    {
        capy::test::fuse f;
        capy::test::write_stream mock(f);
        capy::any_write_stream stream{&mock};
        capy::test::run_blocking()(
            send_message(stream, {"HDR", "BODY"}));
        BOOST_TEST(mock.data() == "HDRBODY");
    }

    void
    testTransports()
    {
        capy::test::run_blocking()(
            use_transports({"HDR", "BODY"}));
    }

    void
    testGuidelines()
    {
        tcp::socket socket;
        capy::test::run_blocking()(guidelines_6f::caller(socket));
        capy::test::run_blocking()(guidelines_6f::wrap_if_needed());
    }

    void
    testUserCode()
    {
        capy::test::run_blocking()(user_code());
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
capy::task<http_response> send_request(capy::any_stream&, http_request const&)
{
    capy::test::fuse f;
    capy::test::read_stream body(f);
    body.provide("{}");
    co_return http_response{200, {}, capy::any_read_stream(std::move(body))};
}

TEST_SUITE(isolation_test, "boost.capy.doc.6f_isolation");
