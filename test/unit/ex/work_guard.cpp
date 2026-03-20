//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/work_guard.hpp>

#include <boost/capy/ex/execution_context.hpp>

#include <type_traits>
#include <utility>

#include "test_suite.hpp"

namespace boost {
namespace capy {

// Minimal execution context for testing
struct guard_test_context : public execution_context
{
    int work_count = 0;
};

// Test executor that tracks work calls
struct guard_test_executor
{
    guard_test_context* ctx_ = nullptr;

    guard_test_executor() = default;

    explicit
    guard_test_executor(guard_test_context& ctx) noexcept
        : ctx_(&ctx)
    {
    }

    bool
    operator==(guard_test_executor const& other) const noexcept
    {
        return ctx_ == other.ctx_;
    }

    guard_test_context&
    context() const noexcept
    {
        return *ctx_;
    }

    void
    on_work_started() const noexcept
    {
        ++ctx_->work_count;
    }

    void
    on_work_finished() const noexcept
    {
        --ctx_->work_count;
    }

    std::coroutine_handle<>
    dispatch(continuation& c) const
    {
        return c.h;
    }

    void
    post(continuation&) const
    {
    }
};

// Verify Executor concept
static_assert(Executor<guard_test_executor>);

struct work_guard_test
{
    void
    run()
    {
        // Construction calls on_work_started()
        {
            guard_test_context ctx;
            BOOST_TEST(ctx.work_count == 0);

            {
                guard_test_executor ex(ctx);
                work_guard<guard_test_executor> guard(ex);
                BOOST_TEST(ctx.work_count == 1);
                BOOST_TEST(guard.owns_work());
            }

            BOOST_TEST(ctx.work_count == 0);
        }

        // Destruction calls on_work_finished()
        {
            guard_test_context ctx;

            {
                guard_test_executor ex(ctx);
                work_guard<guard_test_executor> guard(ex);
                BOOST_TEST(ctx.work_count == 1);
            }

            BOOST_TEST(ctx.work_count == 0);
        }

        // Copy constructor increments work count
        {
            guard_test_context ctx;
            guard_test_executor ex(ctx);

            work_guard<guard_test_executor> guard1(ex);
            BOOST_TEST(ctx.work_count == 1);

            {
                work_guard<guard_test_executor> guard2(guard1);
                BOOST_TEST(ctx.work_count == 2);
                BOOST_TEST(guard1.owns_work());
                BOOST_TEST(guard2.owns_work());
                BOOST_TEST(guard1.executor() == guard2.executor());
            }

            BOOST_TEST(ctx.work_count == 1);
        }

        // Move constructor transfers ownership (no extra calls)
        {
            guard_test_context ctx;
            guard_test_executor ex(ctx);

            work_guard<guard_test_executor> guard1(ex);
            BOOST_TEST(ctx.work_count == 1);

            work_guard<guard_test_executor> guard2(std::move(guard1));
            BOOST_TEST(ctx.work_count == 1);  // No change
            BOOST_TEST(!guard1.owns_work());
            BOOST_TEST(guard2.owns_work());
        }

        // reset() releases work early
        {
            guard_test_context ctx;
            guard_test_executor ex(ctx);

            work_guard<guard_test_executor> guard(ex);
            BOOST_TEST(ctx.work_count == 1);
            BOOST_TEST(guard.owns_work());

            guard.reset();
            BOOST_TEST(ctx.work_count == 0);
            BOOST_TEST(!guard.owns_work());

            // Second reset has no effect
            guard.reset();
            BOOST_TEST(ctx.work_count == 0);
            BOOST_TEST(!guard.owns_work());
        }

        // owns_work() returns correct state
        {
            guard_test_context ctx;
            guard_test_executor ex(ctx);

            work_guard<guard_test_executor> guard(ex);
            BOOST_TEST(guard.owns_work() == true);

            guard.reset();
            BOOST_TEST(guard.owns_work() == false);
        }

        // executor() returns correct executor
        {
            guard_test_context ctx;
            guard_test_executor ex(ctx);

            work_guard<guard_test_executor> guard(ex);
            BOOST_TEST(guard.executor() == ex);
            BOOST_TEST(&guard.executor().context() == &ctx);
        }

        // executor() returns a stable reference
        {
            guard_test_context ctx;
            guard_test_executor ex(ctx);

            work_guard<guard_test_executor> guard(ex);
            auto const& ref1 = guard.executor();
            auto const& ref2 = guard.executor();
            BOOST_TEST(&ref1 == &ref2);
        }

        // executor() returns a const lvalue reference
        {
            using guard_type =
                work_guard<guard_test_executor>;
            static_assert(std::is_same_v<
                decltype(std::declval<guard_type const&>().executor()),
                guard_test_executor const&>);
        }

        // make_work_guard(Executor) factory function
        {
            guard_test_context ctx;
            guard_test_executor ex(ctx);

            auto guard = make_work_guard(ex);
            BOOST_TEST(ctx.work_count == 1);
            BOOST_TEST(guard.owns_work());
            BOOST_TEST(guard.executor() == ex);
        }

        // Copy from guard that doesn't own work
        {
            guard_test_context ctx;
            guard_test_executor ex(ctx);

            work_guard<guard_test_executor> guard1(ex);
            guard1.reset();
            BOOST_TEST(ctx.work_count == 0);

            work_guard<guard_test_executor> guard2(guard1);
            BOOST_TEST(ctx.work_count == 0);  // No increment
            BOOST_TEST(!guard2.owns_work());
        }
    }
};

TEST_SUITE(
    work_guard_test,
    "boost.capy.work_guard");

} // capy
} // boost
