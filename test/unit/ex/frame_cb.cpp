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
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/io_result.hpp>

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

        auto h = std::coroutine_handle<>::from_address(
            static_cast<void*>(&cb));

        h.resume();
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

        auto h = std::coroutine_handle<>::from_address(
            static_cast<void*>(&cb));

        h.destroy();
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

        auto h = std::coroutine_handle<>::from_address(
            static_cast<void*>(&cb));

        h.resume();
        BOOST_TEST_EQ(value, 42);
    }

    void
    testWithIoAwaitable()
    {
        struct test_awaitable
        {
            int* result;

            bool await_ready() const noexcept
            {
                return false;
            }

            std::coroutine_handle<>
            await_suspend(
                std::coroutine_handle<> h,
                io_env const*) noexcept
            {
                h.resume();
                return std::noop_coroutine();
            }

            io_result<> await_resume() noexcept
            {
                *result = 99;
                return {};
            }
        };

        static_assert(IoAwaitable<test_awaitable>);

        int result = 0;
        bool resumed = false;

        struct state
        {
            test_awaitable* aw;
            bool* resumed;
        };

        state st{nullptr, &resumed};
        test_awaitable aw{&result};
        st.aw = &aw;

        detail::frame_cb cb;
        cb.resume = +[](detail::frame_cb* p) {
            auto* s = static_cast<state*>(p->data);
            (void)s->aw->await_resume();
            *s->resumed = true;
        };
        cb.destroy = +[](detail::frame_cb*) {};
        cb.data = &st;

        int dispatch_count = 0;
        test_executor ex(dispatch_count);
        io_env env{ex, {}};

        auto h = std::coroutine_handle<>::from_address(
            static_cast<void*>(&cb));

        detail::call_await_suspend(&aw, h, &env);

        BOOST_TEST(resumed);
        BOOST_TEST_EQ(result, 99);
    }

    void
    testWithAsyncAwaitable()
    {
        thread_pool pool(1);
        std::latch done(1);
        bool resumed = false;

        struct state
        {
            bool* resumed;
            std::latch* done;
        };

        state st{&resumed, &done};

        detail::frame_cb cb;
        cb.resume = +[](detail::frame_cb* p) {
            auto* s = static_cast<state*>(p->data);
            *s->resumed = true;
            s->done->count_down();
        };
        cb.destroy = +[](detail::frame_cb*) {};
        cb.data = &st;

        struct post_awaitable
        {
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

            void await_resume() noexcept {}
        };

        static_assert(IoAwaitable<post_awaitable>);

        post_awaitable aw;
        io_env env{pool.get_executor(), {}};

        auto h = std::coroutine_handle<>::from_address(
            static_cast<void*>(&cb));

        detail::call_await_suspend(&aw, h, &env);

        done.wait();
        BOOST_TEST(resumed);
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
