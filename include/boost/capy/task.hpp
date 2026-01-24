//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/corosio
//

#ifndef BOOST_CAPY_TASK_HPP
#define BOOST_CAPY_TASK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/io_awaitables.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/frame_allocator.hpp>

#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace boost {
namespace capy {

namespace detail {

// Helper base for result storage and return_void/return_value
template<typename T>
struct task_return_base
{
    std::optional<T> result_;

    void return_value(T value)
    {
        result_ = std::move(value);
    }

    T&& result() noexcept
    {
        return std::move(*result_);
    }
};

template<>
struct task_return_base<void>
{
    void return_void()
    {
    }
};

} // namespace detail

/** A coroutine task type implementing the affine awaitable protocol.

    This task type represents an asynchronous operation that can be awaited.
    It implements the affine awaitable protocol where `await_suspend` receives
    the caller's executor, enabling proper completion dispatch across executor
    boundaries.

    @tparam T The return type of the task. Defaults to void.

    Key features:
    @li Lazy execution - the coroutine does not start until awaited
    @li Symmetric transfer - uses coroutine handle returns for efficient
        resumption
    @li Executor inheritance - inherits caller's executor unless explicitly
        bound

    The task uses `[[clang::coro_await_elidable]]` (when available) to enable
    heap allocation elision optimization (HALO) for nested coroutine calls.

    @see executor_ref
*/
template<typename T = void>
struct [[nodiscard]] BOOST_CAPY_CORO_AWAIT_ELIDABLE
    task
{
    struct promise_type
        : io_awaitable_support<promise_type>
        , detail::task_return_base<T>
    {
        std::exception_ptr ep_;

        std::exception_ptr exception() const noexcept
        {
            return ep_;
        }

        task get_return_object()
        {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto initial_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;

                bool await_ready() const noexcept
                {
                    return false;
                }

                void await_suspend(coro) const noexcept
                {
                    // Capture TLS allocator while it's still valid
                    p_->set_frame_allocator(current_frame_allocator());
                }

                void await_resume() const noexcept
                {
                    // Restore TLS when body starts executing
                    if(p_->frame_allocator())
                        current_frame_allocator() = p_->frame_allocator();
                }
            };
            return awaiter{this};
        }

        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;

                bool await_ready() const noexcept
                {
                    return false;
                }

                coro await_suspend(coro) const noexcept
                {
                    return p_->complete();
                }

                void await_resume() const noexcept
                {
                }
            };
            return awaiter{this};
        }

        void unhandled_exception()
        {
            ep_ = std::current_exception();
        }

        template<class Awaitable>
        struct transform_awaiter
        {
            std::decay_t<Awaitable> a_;
            promise_type* p_;

            bool await_ready()
            {
                return a_.await_ready();
            }

            auto await_resume()
            {
                // Restore TLS before body resumes
                if(p_->frame_allocator())
                    current_frame_allocator() = p_->frame_allocator();
                return a_.await_resume();
            }

            template<class Promise>
            auto await_suspend(std::coroutine_handle<Promise> h)
            {
                return a_.await_suspend(h, p_->executor(), p_->stop_token());
            }
        };

        template<class Awaitable>
        auto transform_awaitable(Awaitable&& a)
        {
            using A = std::decay_t<Awaitable>;
            if constexpr (IoAwaitable<A>)
            {
                return transform_awaiter<Awaitable>{
                    std::forward<Awaitable>(a), this};
            }
            else
            {
                static_assert(sizeof(A) == 0, "requires IoAwaitable");
            }
        }
    };

    std::coroutine_handle<promise_type> h_;

    ~task()
    {
        if(h_)
            h_.destroy();
    }

    bool await_ready() const noexcept
    {
        return false;
    }

    auto await_resume()
    {
        if(h_.promise().ep_)
            std::rethrow_exception(h_.promise().ep_);
        if constexpr (! std::is_void_v<T>)
            return std::move(*h_.promise().result_);
        else
            return;
    }

    // IoAwaitable: receive caller's executor and stop_token for completion dispatch
    coro await_suspend(coro cont, executor_ref caller_ex, std::stop_token token)
    {
        h_.promise().set_continuation(cont, caller_ex);
        h_.promise().set_executor(caller_ex);
        h_.promise().set_stop_token(token);
        return h_;
    }

    /** Return the coroutine handle.

        @return The coroutine handle.
    */
    std::coroutine_handle<promise_type> handle() const noexcept
    {
        return h_;
    }

    /** Release ownership of the coroutine handle.

        After calling this, the task no longer owns the handle and will
        not destroy it. The caller is responsible for the handle's lifetime.
    */
    void release() noexcept
    {
        h_ = nullptr;
    }

    // Non-copyable
    task(task const&) = delete;
    task& operator=(task const&) = delete;

    // Movable
    task(task&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    task& operator=(task&& other) noexcept
    {
        if(this != &other)
        {
            if(h_)
                h_.destroy();
            h_ = std::exchange(other.h_, nullptr);
        }
        return *this;
    }

private:
    explicit task(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
};

} // namespace capy
} // namespace boost

#endif
