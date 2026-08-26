//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::any_stream, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/io/any_stream.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

#include "../doc_warnings.hpp"

#include <boost/capy.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace capy = boost::capy;

namespace {

namespace ex_1 {
// tag::example[]
// A minimal bidirectional stream: both operations complete
// immediately, reporting the whole buffer sequence transferred.
struct instant_stream
{
    template<capy::MutableBufferSequence MB>
    auto read_some(MB buffers)
    {
        return capy::ready(capy::buffer_size(buffers));
    }

    template<capy::ConstBufferSequence CB>
    auto write_some(CB buffers)
    {
        return capy::ready(capy::buffer_size(buffers));
    }
};

void reader(capy::any_read_stream&) {}
void writer(capy::any_write_stream&) {}

capy::task<> use_any_stream()
{
    // Owning - takes ownership of the stream
    capy::any_stream owning_stream(instant_stream{});

    // Reference - wraps without ownership
    instant_stream instant;
    capy::any_stream ref_stream(&instant);

    // Use read_some from the any_read_stream base
    char rdata[1024];
    capy::mutable_buffer rbuf(rdata, sizeof(rdata));
    auto [ec1, n1] = co_await owning_stream.read_some(std::span(&rbuf, 1));

    // Use write_some from the any_write_stream base
    char wdata[] = "hello";
    capy::const_buffer wbuf(wdata, sizeof(wdata));
    auto [ec2, n2] = co_await owning_stream.write_some(std::span(&wbuf, 1));

    // Pass to functions expecting one capability
    reader(owning_stream);  // Implicit upcast
    writer(owning_stream);  // Implicit upcast
}
// end::example[]
} // namespace ex_1

} // namespace
