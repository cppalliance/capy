//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::buffer_param, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/buffers/buffer_param.hpp
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
// tag::example_1[]
task<> write(ConstBufferSequence auto buffers);   // CORRECT
task<> write(ConstBufferSequence auto& buffers);  // WRONG - dangling reference
// end::example_1[]
} // namespace ex_1
namespace ex_2 {
// tag::example_2[]
task<> send(ConstBufferSequence auto buffers)
{
    buffer_param bp(buffers);
    while(true)
    {
        auto bufs = bp.data();
        if(bufs.empty())
            break;
        auto n = co_await do_something(bufs);
        bp.consume(n);
    }
}
// end::example_2[]
} // namespace ex_2
namespace ex_3 {
// tag::example_3[]
class base
{
public:
    template<ConstBufferSequence BS>
    task<> write(BS buffers)
    {
        const_buffer_param<BS> bp(buffers);
        while(true)
        {
            auto bufs = bp.data();
            if(bufs.empty())
                break;
            std::size_t n = 0;
            co_await write_impl(bufs, n);
            bp.consume(n);
        }
    }

protected:
    virtual task<> write_impl(
        std::span<const_buffer> buffers,
        std::size_t& bytes_written) = 0;
};
// end::example_3[]
} // namespace ex_3

} // namespace
