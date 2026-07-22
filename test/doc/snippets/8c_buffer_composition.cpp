//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/8.examples/8c.buffer-composition.adoc.

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
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/write.hpp>

#include <array>
#include <string>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

capy::task<>
send_response(
    capy::any_stream& stream,
    std::array<capy::const_buffer, 5> const& http_response)
{
    // tag::write_gather[]
    co_await write(stream, http_response);
    // end::write_gather[]
}

struct buffer_composition_test
{
    void
    testWriteGather()
    {
        std::string status = "HTTP/1.1 200 OK\r\n";
        std::string content_type = "Content-Type: application/json\r\n";
        std::string server = "Server: Capy/1.0\r\n";
        std::string empty_line = "\r\n";
        std::string body = R"({"status":"ok"})";

        std::array<capy::const_buffer, 5> http_response = {{
            capy::make_buffer(status),
            capy::make_buffer(content_type),
            capy::make_buffer(server),
            capy::make_buffer(empty_line),
            capy::make_buffer(body)
        }};

        auto [a, b] = capy::test::make_stream_pair();
        capy::any_stream stream{&a};

        capy::test::run_blocking()(send_response(stream, http_response));

        BOOST_TEST(b.data() ==
            status + content_type + server + empty_line + body);
    }

    void
    run()
    {
        testWriteGather();
    }
};

} // namespace

TEST_SUITE(buffer_composition_test, "boost.capy.doc.8c_buffer_composition");
