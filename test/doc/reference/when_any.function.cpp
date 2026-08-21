//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::when_any, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/when_any.hpp
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
io_task<std::size_t> read_from( any_read_stream& stream, mutable_buffer buf )
{
    co_return co_await stream.read_some(buf);
}

task<void> read_first_ready(
    std::vector<any_read_stream>& streams, std::vector<mutable_buffer>& buffers )
{
    // One awaitable per stream: each stream is touched by exactly one
    // child, so racing them concurrently is safe.
    std::vector<io_task<std::size_t>> reads;
    for (std::size_t i = 0; i < streams.size(); ++i)
        reads.push_back(read_from(streams[i], buffers[i]));

    auto result = co_await when_any(std::move(reads));
    if (result.index() == 1)
    {
        auto [idx, n] = std::get<1>(result);  // winning stream's slot and byte count
    }
}
// end::example_1[]
} // namespace ex_1
namespace ex_2 {
// tag::example_2[]
io_task<> background_work_a()
{
    co_return {};
}

io_task<> background_work_b()
{
    co_return {};
}

task<void> example()
{
    std::vector<io_task<>> jobs;
    jobs.push_back(background_work_a());
    jobs.push_back(background_work_b());

    auto result = co_await when_any(std::move(jobs));
    if (result.index() == 1)
    {
        auto winner = std::get<1>(result);  // index of the job that succeeded first
    }
}
// end::example_2[]
} // namespace ex_2

} // namespace
