//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include "test/unit/test_helpers.hpp"

#include <coroutine>
#include <latch>

namespace boost {
namespace capy {

namespace detail {

struct frame_cb
{
    void (*resume)(frame_cb*);
    void (*destroy)(frame_cb*);
    void* data;
};

} // detail

struct frame_cb_test
{
    void
    testResumeCallsFunctionPointer()
    {
        bool called = false;
        detail::frame_cb cb;
        cb.resume = +[](detail::frame_cb* p) {
            *static_cast<bool*>(p->data) = true;
        };
        cb.destroy = +[](detail::frame_cb*) {};
        cb.data = &called;

        cb.resume(&cb);
        BOOST_TEST(called);
    }

    void
    testDestroyIsNoOp()
    {
        bool destroy_called = false;
        detail::frame_cb cb;
        cb.resume = +[](detail::frame_cb*) {};
        cb.destroy = +[](detail::frame_cb* p) {
            *static_cast<bool*>(p->data) = true;
        };
        cb.data = &destroy_called;

        cb.destroy(&cb);
        BOOST_TEST(destroy_called);
    }

    void
    testDataPointerPassedThrough()
    {
        int value = 0;
        detail::frame_cb cb;
        cb.resume = +[](detail::frame_cb* p) {
            *static_cast<int*>(p->data) = 42;
        };
        cb.destroy = +[](detail::frame_cb*) {};
        cb.data = &value;

        cb.resume(&cb);
        BOOST_TEST_EQ(value, 42);
    }

    // IoAwaitable that resumes synchronously and returns a value
    struct sync_awaitable
    {
        int value;

        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h,
            io_env const*) noexcept
        {
            return h;
        }

        io_result<int> await_resume() noexcept
        {
            return {std::error_code{}, value};
        }
    };

    static_assert(IoAwaitable<sync_awaitable>);

    static task<int>
    await_sync(int v)
    {
        auto [ec, result] = co_await sync_awaitable{v};
        co_return result;
    }

    void
    testWithIoAwaitable()
    {
        int result = 0;
        test::run_blocking(
            [&](int v) { result = v; })(
            await_sync(99));
        BOOST_TEST_EQ(result, 99);
    }

    // IoAwaitable that posts to executor (async resume)
    struct async_awaitable
    {
        int value;

        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h,
            io_env const* env) noexcept
        {
            env->executor.post(h);
            return std::noop_coroutine();
        }

        io_result<int> await_resume() noexcept
        {
            return {std::error_code{}, value};
        }
    };

    static_assert(IoAwaitable<async_awaitable>);

    static task<int>
    await_async(int v)
    {
        auto [ec, result] = co_await async_awaitable{v};
        co_return result;
    }

    void
    testWithAsyncAwaitable()
    {
        thread_pool pool(1);
        std::latch done(1);
        int result = 0;

        run_async(pool.get_executor(),
            [&](int v) {
                result = v;
                done.count_down();
            },
            [&](std::exception_ptr) {
                done.count_down();
            })(await_async(99));

        done.wait();
        BOOST_TEST_EQ(result, 99);
    }

    void
    run()
    {
        testResumeCallsFunctionPointer();
        testDestroyIsNoOp();
        testDataPointerPassedThrough();
        testWithIoAwaitable();
        testWithAsyncAwaitable();
    }
};

TEST_SUITE(frame_cb_test, "capy.frame_cb");

} // capy
} // boost
