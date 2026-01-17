//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/execution_context.hpp>

#include <utility>

#include "test_suite.hpp"

namespace boost {
namespace capy {

// Minimal execution context for testing
struct test_context
{
    int id = 0;
};

// Test executor that satisfies the concept
struct test_executor
{
    test_context* ctx_ = nullptr;

    test_executor() = default;

    explicit
    test_executor(test_context& ctx) noexcept
        : ctx_(&ctx)
    {
    }

    // Equality comparison (required by Networking TS)
    bool
    operator==(test_executor const& other) const noexcept
    {
        return ctx_ == other.ctx_;
    }

    // Execution context access
    test_context&
    context() const noexcept
    {
        return *ctx_;
    }

    // Work tracking
    void
    on_work_started() const noexcept
    {
    }

    void
    on_work_finished() const noexcept
    {
    }

    // Work submission
    std::coroutine_handle<>
    operator()(std::coroutine_handle<> h) const
    {
        return h;
    }

    void
    post(std::coroutine_handle<>) const
    {
    }

    void
    defer(std::coroutine_handle<>) const
    {
    }
};

// Verify executor concept
static_assert(executor<test_executor>);

struct executor_test
{
    void
    run()
    {
        // executor - equality comparison
        {
            test_context ctx1;
            test_context ctx2;
            test_executor e1(ctx1);
            test_executor e2(ctx1);
            test_executor e3(ctx2);

            BOOST_TEST(e1 == e2);
            BOOST_TEST(!(e1 != e2));
            BOOST_TEST(e1 != e3);
            BOOST_TEST(!(e1 == e3));
        }

        // executor - context() returns same reference for equal executors
        {
            test_context ctx;
            test_executor e1(ctx);
            test_executor e2(ctx);

            BOOST_TEST(e1 == e2);
            BOOST_TEST(&e1.context() == &e2.context());
            BOOST_TEST(&e1.context() == &ctx);
        }

        // executor - copy preserves context
        {
            test_context ctx;
            test_executor e1(ctx);
            test_executor e2(e1);

            BOOST_TEST(e1 == e2);
            BOOST_TEST(&e1.context() == &e2.context());
        }
    }
};

TEST_SUITE(
    executor_test,
    "boost.capy.executor");

} // capy
} // boost
