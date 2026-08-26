//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// The library-API header shown in pages/6.streams/6f.isolation.adoc.
// The page presents this file as http_client.hpp; scaffolding stays
// outside the tags.

#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/task.hpp>

#include <map>
#include <string>

namespace capy = boost::capy;

// tag::http_client_header[]
// http_client.hpp
#pragma once
#include <boost/capy/io/any_read_stream.hpp>

struct http_request
{
    std::string method;
    std::string url;
    std::map<std::string, std::string> headers;
};

struct http_response
{
    int status_code;
    std::map<std::string, std::string> headers;
    capy::any_read_stream body;  // Body is read as a stream
};

// Send request, receive response
// Works with any transport that provides any_stream
capy::task<http_response> send_request(
    capy::any_stream& conn, http_request const& req);
// end::http_client_header[]
