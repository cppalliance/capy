//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/run_on.hpp>

#include <boost/capy/io_awaitable.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>

#include "test/unit/custom_task.hpp"
#include "test/unit/test_helpers.hpp"

namespace boost {
namespace capy {

static_assert(IoAwaitable<detail::run_on_awaitable<task<void>, executor_ref>>);
static_assert(IoAwaitable<detail::run_on_awaitable<task<int>, executor_ref>>);

using test::custom_task;

//----------------------------------------------------------
// run_on Tests
//----------------------------------------------------------

struct run_on_test
{
    static custom_task<int>
    custom_returns_int()
    {
        co_return 456;
    }

    static custom_task<void>
    custom_returns_void()
    {
        co_return;
    }

    void
    testCustomTaskType()
    {
        // Proves run_on works with any IoLaunchableTask, not just capy::task
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        int result = 0;

        auto outer = [&]() -> task<int> {
            co_return co_await run_on(ex, custom_returns_int());
        };

        run_async(ex, [&](int v) { result = v; })(outer());

        BOOST_TEST_EQ(result, 456);
    }

    void
    testCustomTaskTypeVoid()
    {
        // Proves run_on works with void custom tasks
        int dispatch_count = 0;
        test_executor ex(1, dispatch_count);
        bool called = false;

        auto outer = [&]() -> task<void> {
            co_await run_on(ex, custom_returns_void());
        };

        run_async(ex, [&]() { called = true; })(outer());

        BOOST_TEST(called);
    }

    void
    run()
    {
        testCustomTaskType();
        testCustomTaskTypeVoid();
    }
};

TEST_SUITE(
    run_on_test,
    "boost.capy.ex.run_on");

} // capy
} // boost
