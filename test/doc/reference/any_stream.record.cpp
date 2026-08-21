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

// Examples leave results unused; the reference explains them in prose.
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

#include <boost/capy.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace capy = boost::capy;
using namespace boost::capy;

namespace {

namespace ex_1 {
// tag::example[]
// A minimal bidirectional stream: both operations complete
// immediately, reporting the whole buffer sequence transferred.
struct instant_stream
{
    template<MutableBufferSequence MB>
    auto read_some(MB buffers)
    {
        return ready(buffer_size(buffers));
    }

    template<ConstBufferSequence CB>
    auto write_some(CB buffers)
    {
        return ready(buffer_size(buffers));
    }
};

void reader(any_read_stream&) {}
void writer(any_write_stream&) {}

task<> use_any_stream()
{
    // Owning - takes ownership of the stream
    any_stream owning_stream(instant_stream{});

    // Reference - wraps without ownership
    instant_stream instant;
    any_stream ref_stream(&instant);

    // Use read_some from the any_read_stream base
    char rdata[1024];
    mutable_buffer rbuf(rdata, sizeof(rdata));
    auto [ec1, n1] = co_await owning_stream.read_some(std::span(&rbuf, 1));

    // Use write_some from the any_write_stream base
    char wdata[] = "hello";
    const_buffer wbuf(wdata, sizeof(wdata));
    auto [ec2, n2] = co_await owning_stream.write_some(std::span(&wbuf, 1));

    // Pass to functions expecting one capability
    reader(owning_stream);  // Implicit upcast
    writer(owning_stream);  // Implicit upcast
}
// end::example[]
} // namespace ex_1

} // namespace
