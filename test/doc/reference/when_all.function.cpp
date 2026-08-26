//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::when_all, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/when_all.hpp
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
// tag::example_1[]
capy::io_task<std::size_t> read_one( capy::any_read_stream& stream, capy::mutable_buffer buf )
{
    co_return co_await stream.read_some(buf);
}

capy::task<void> read_all_connections(
    std::vector<capy::any_read_stream>& streams, std::vector<capy::mutable_buffer>& buffers )
{
    // One awaitable per stream: each stream is touched by exactly one
    // child, so running them concurrently is safe.
    std::vector<capy::io_task<std::size_t>> reads;
    for (std::size_t i = 0; i < streams.size(); ++i)
        reads.push_back(read_one(streams[i], buffers[i]));

    auto [ec, counts] = co_await capy::when_all(std::move(reads));
    if (ec)
    {
        // handle error
    }
}
// end::example_1[]
} // namespace ex_1
namespace ex_2 {
// tag::example_2[]
template< class MakeJob >
capy::task<void> run_n_jobs( MakeJob make_job, int n )
{
    std::vector<capy::io_task<>> jobs;
    for (int i = 0; i < n; ++i)
        jobs.push_back(make_job(i));  // io_task<> per index

    auto [ec] = co_await capy::when_all(std::move(jobs));
}
// end::example_2[]
} // namespace ex_2

} // namespace
