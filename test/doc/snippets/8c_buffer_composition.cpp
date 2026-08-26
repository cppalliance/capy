//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/8.examples/8c.buffer-composition.adoc.

#include "../doc_warnings.hpp"

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
    co_await capy::write(stream, http_response);
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
