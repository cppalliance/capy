//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/executor.hpp>
#include <boost/capy/execution_context.hpp>

#include <utility>

#include "test_suite.hpp"

namespace boost {
namespace capy {

// Test handler implementation
struct test_handler : execution_context::handler
{
    int& invoked;
    int& destroyed;

    test_handler(int& i, int& d)
        : invoked(i)
        , destroyed(d)
    {
    }

    void operator()() override
    {
        ++invoked;
    }

    void destroy() override
    {
        ++destroyed;
    }
};

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
    dispatch(std::coroutine_handle<> h) const
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
        // handler - invoke operator()
        {
            int invoked = 0;
            int destroyed = 0;
            test_handler h(invoked, destroyed);

            h();

            BOOST_TEST(invoked == 1);
            BOOST_TEST(destroyed == 0);
        }

        // handler - invoke destroy()
        {
            int invoked = 0;
            int destroyed = 0;
            test_handler h(invoked, destroyed);

            h.destroy();

            BOOST_TEST(invoked == 0);
            BOOST_TEST(destroyed == 1);
        }

        // queue - default construction
        {
            execution_context::queue q;
            BOOST_TEST(q.empty());
            BOOST_TEST(q.pop() == nullptr);
        }

        // queue - push and pop
        {
            int invoked = 0;
            int destroyed = 0;
            test_handler h(invoked, destroyed);

            execution_context::queue q;
            q.push(&h);
            BOOST_TEST(!q.empty());

            execution_context::handler* p = q.pop();
            BOOST_TEST(p == &h);
            BOOST_TEST(q.empty());
        }

        // queue - FIFO order
        {
            int invoked1 = 0, destroyed1 = 0;
            int invoked2 = 0, destroyed2 = 0;
            int invoked3 = 0, destroyed3 = 0;
            test_handler h1(invoked1, destroyed1);
            test_handler h2(invoked2, destroyed2);
            test_handler h3(invoked3, destroyed3);

            execution_context::queue q;
            q.push(&h1);
            q.push(&h2);
            q.push(&h3);

            BOOST_TEST(q.pop() == &h1);
            BOOST_TEST(q.pop() == &h2);
            BOOST_TEST(q.pop() == &h3);
            BOOST_TEST(q.empty());
        }

        // queue - move constructor
        {
            int invoked = 0;
            int destroyed = 0;
            test_handler h(invoked, destroyed);

            execution_context::queue q1;
            q1.push(&h);

            execution_context::queue q2(std::move(q1));
            BOOST_TEST(q1.empty());
            BOOST_TEST(!q2.empty());
            BOOST_TEST(q2.pop() == &h);
        }

        // queue - splice
        {
            int i1 = 0, d1 = 0;
            int i2 = 0, d2 = 0;
            int i3 = 0, d3 = 0;
            int i4 = 0, d4 = 0;
            test_handler h1(i1, d1);
            test_handler h2(i2, d2);
            test_handler h3(i3, d3);
            test_handler h4(i4, d4);

            execution_context::queue q1;
            execution_context::queue q2;
            q1.push(&h1);
            q1.push(&h2);
            q2.push(&h3);
            q2.push(&h4);

            q1.push(q2);

            BOOST_TEST(q2.empty());
            BOOST_TEST(q1.pop() == &h1);
            BOOST_TEST(q1.pop() == &h2);
            BOOST_TEST(q1.pop() == &h3);
            BOOST_TEST(q1.pop() == &h4);
            BOOST_TEST(q1.empty());
        }

        // queue - destructor calls destroy
        {
            int invoked = 0;
            int destroyed = 0;
            test_handler h(invoked, destroyed);

            {
                execution_context::queue q;
                q.push(&h);
                // destructor should call destroy()
            }

            BOOST_TEST(invoked == 0);
            BOOST_TEST(destroyed == 1);
        }

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
