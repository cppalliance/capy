//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::any_write_stream, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/io/any_write_stream.hpp
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
// A minimal WriteStream: completes immediately, reporting
// the whole buffer sequence as written.
struct instant_stream
{
    template<capy::ConstBufferSequence CB>
    auto write_some(CB buffers)
    {
        return capy::ready(capy::buffer_size(buffers));
    }
};

capy::task<> use_any_write_stream()
{
    // Owning - takes ownership of the stream
    capy::any_write_stream owning_stream(instant_stream{});

    // Reference - wraps without ownership
    instant_stream instant;
    capy::any_write_stream ref_stream(&instant);

    char data[] = "hello";
    capy::const_buffer buf(data, sizeof(data));
    auto [ec, n] = co_await owning_stream.write_some(std::span(&buf, 1));
}
// end::example[]
} // namespace ex_1

} // namespace
