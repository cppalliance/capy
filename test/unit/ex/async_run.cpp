//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/async_run.hpp>

#include <boost/capy/ex/async_op.hpp>
#include <boost/capy/task.hpp>

#include "test_suite.hpp"

#include <queue>

namespace boost {
namespace capy {

struct inline_dispatcher
{
    any_coro operator()(any_coro h) const { return h; }
};

static_assert(dispatcher<inline_dispatcher>);

#if BOOST_CAPY_HAS_STOP_TOKEN

struct async_run_test
{
    static async_op<int>
    async_op_immediate(int value)
    {
        return make_async_op<int>([value](auto cb) { cb(value); });
    }

    void
    testStopTokenPropagation()
    {
        inline_dispatcher d;
        std::stop_source source;
        bool stop_possible = false;

        auto check_token = [&]() -> task<void> {
            auto token = co_await get_stop_token();
            stop_possible = token.stop_possible();
        };

        async_run(d, source.get_token())(check_token(),
            []() {},
            [](std::exception_ptr) {});

        BOOST_TEST(stop_possible);
    }

    void
    testStopTokenDefaultEmpty()
    {
        inline_dispatcher d;
        bool stop_possible = true;

        auto check_token = [&]() -> task<void> {
            auto token = co_await get_stop_token();
            stop_possible = token.stop_possible();
        };

        // No stop_token provided - should get empty token
        async_run(d)(check_token(),
            []() {},
            [](std::exception_ptr) {});

        BOOST_TEST(!stop_possible);
    }

    void
    testStopRequestedPropagates()
    {
        inline_dispatcher d;
        std::stop_source source;
        bool stop_requested = false;

        auto check_token = [&]() -> task<void> {
            auto token = co_await get_stop_token();
            stop_requested = token.stop_requested();
        };

        // Request stop before launching
        source.request_stop();

        async_run(d, source.get_token())(check_token(),
            []() {},
            [](std::exception_ptr) {});

        BOOST_TEST(stop_requested);
    }

    void
    testStopTokenWithAllocator()
    {
        inline_dispatcher d;
        std::stop_source source;
        bool stop_possible = false;

        auto check_token = [&]() -> task<void> {
            auto token = co_await get_stop_token();
            stop_possible = token.stop_possible();
        };

        // Test with both token and allocator
        detail::recycling_frame_allocator alloc;
        async_run(d, source.get_token(), alloc)(check_token(),
            []() {},
            [](std::exception_ptr) {});

        BOOST_TEST(stop_possible);
    }

    void
    run()
    {
        testStopTokenPropagation();
        testStopTokenDefaultEmpty();
        testStopRequestedPropagates();
        testStopTokenWithAllocator();
    }
};

TEST_SUITE(
    async_run_test,
    "boost.capy.ex.async_run");

#endif // BOOST_CAPY_HAS_STOP_TOKEN

} // namespace capy
} // namespace boost