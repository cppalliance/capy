//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/9.design/9o.WhyNotTMC.adoc.

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

using boost::capy::io_env;

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
    auto await_suspend(std::coroutine_handle<> h, io_env const* env);
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
        using capy::run_async;
        // tag::arena[]
        std::pmr::monotonic_buffer_resource arena;
        run_async(ex, &arena)(handle_connection(socket));
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
