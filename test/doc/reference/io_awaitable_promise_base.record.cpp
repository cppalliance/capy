//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::io_awaitable_promise_base, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/io_awaitable_promise_base.hpp
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
// Both examples on this page show the same minimal promise_type
// deliberately: the point here is the mixin's environment access, not
// variations on promise plumbing.
struct my_task
{
    struct promise_type : capy::io_awaitable_promise_base<promise_type>
    {
        my_task get_return_object()
        {
            return my_task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }

        // Resumes the awaiting coroutine by symmetric transfer, as the
        // base class requires: the continuation stored by
        // set_continuation() must be handed back here, or the awaiting
        // coroutine is never resumed.
        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() const noexcept { return false; }
                std::coroutine_handle<>
                await_suspend(std::coroutine_handle<>) const noexcept
                {
                    return p_->continuation();
                }
                void await_resume() const noexcept {}
            };
            return awaiter{this};
        }
        void return_void() {}

        // Capture rather than swallow. A real awaitable rethrows
        // this from its own await_resume().
        void unhandled_exception() noexcept { ep_ = std::current_exception(); }

        std::exception_ptr ep_;
    };

    std::coroutine_handle<promise_type> h;
};

my_task example()
{
    auto env = co_await capy::this_coro::environment;
    // Access env->executor, env->stop_token, env->frame_allocator

    // Or use fine-grained accessors:
    auto ex = co_await capy::this_coro::executor;
    auto token = co_await capy::this_coro::stop_token;
    auto* alloc = co_await capy::this_coro::frame_allocator;
}
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
struct promise_type : capy::io_awaitable_promise_base<promise_type>
{
    template<typename A>
    auto transform_awaitable(A&& a)
    {
        // Your custom transformation logic
        return std::forward<A>(a);
    }
};
// end::example_2[]
} // namespace ex_2

namespace ex_3 {
// tag::example_3[]
// Same minimal promise_type as the Basic Usage example above -- this
// section's point is the await_suspend overload below, not the promise.
struct my_task
{
    struct promise_type : capy::io_awaitable_promise_base<promise_type>
    {
        my_task get_return_object()
        {
            return my_task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }

        // Resumes the awaiting coroutine by symmetric transfer, as the
        // base class requires: the continuation stored by
        // set_continuation() must be handed back here, or the awaiting
        // coroutine is never resumed.
        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() const noexcept { return false; }
                std::coroutine_handle<>
                await_suspend(std::coroutine_handle<>) const noexcept
                {
                    return p_->continuation();
                }
                void await_resume() const noexcept {}
            };
            return awaiter{this};
        }
        void return_void() {}

        // Capture rather than swallow. A real awaitable rethrows
        // this from its own await_resume().
        void unhandled_exception() noexcept { ep_ = std::current_exception(); }

        std::exception_ptr ep_;
    };

    std::coroutine_handle<promise_type> h_;

    // IoAwaitable await_suspend receives and stores the environment,
    // then resumes into the coroutine body via symmetric transfer
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont, capy::io_env const* env)
    {
        h_.promise().set_continuation(cont);
        h_.promise().set_environment(env);
        return h_;
    }
};
// end::example_3[]
} // namespace ex_3

} // namespace
