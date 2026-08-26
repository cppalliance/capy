//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/9.design/9o.WhyNotTMC.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/task.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include <coroutine>
#include <memory_resource>
#include <type_traits>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

// The page shows the three standard await_suspend forms as one listing;
// they differ only in return type, so each lives in its own scaffolding
// struct and the page stitches the tags together.
struct plain_awaitable_void
{
    // tag::std_suspend_void[]
    void await_suspend(std::coroutine_handle<> h);
    // end::std_suspend_void[]
};

struct plain_awaitable_bool
{
    // tag::std_suspend_bool[]
    // or
    bool await_suspend(std::coroutine_handle<> h);
    // end::std_suspend_bool[]
};

struct plain_awaitable_handle
{
    // tag::std_suspend_handle[]
    // or
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h);
    // end::std_suspend_handle[]
};

struct io_awaitable_form
{
    // tag::await_suspend_env[]
    auto await_suspend(std::coroutine_handle<> h, capy::io_env const* env);
    // end::await_suspend_env[]
};

// Mirrors the shape of task.hpp's transform_awaitable(); never
// instantiated with a non-IoAwaitable, so the else branch only
// has to parse.
template<class Awaitable>
void protocol_strictness_shape()
{
    using A = std::decay_t<Awaitable>;
    if constexpr (capy::IoAwaitable<A>)
    {
    }
    // tag::strict_static_assert[]
    // From task.hpp transform_awaitable()
    else
    {
        static_assert(sizeof(A) == 0, "requires IoAwaitable");
    }
    // end::strict_static_assert[]
}

struct connection { };

capy::task<> handle_connection(connection)
{
    co_return;
}

struct why_not_tmc_test
{
    void
    testArenaAllocation()
    {
        capy::thread_pool pool(1);
        auto ex = pool.get_executor();
        connection socket;
        // tag::arena[]
        std::pmr::monotonic_buffer_resource arena;
        capy::run_async(ex, &arena)(handle_connection(socket));
        // On disconnect: entire arena reclaimed instantly
        // end::arena[]
        pool.join();
        BOOST_TEST(true);
    }

    void
    run()
    {
        testArenaAllocation();
    }
};

} // namespace

TEST_SUITE(why_not_tmc_test, "boost.capy.doc.9o_why_not_tmc");
