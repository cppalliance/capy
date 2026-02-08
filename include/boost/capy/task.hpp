//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
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
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/io_awaitable_support.hpp>
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

/** Lazy coroutine task satisfying @ref IoLaunchableTask.

    Use `task<T>` as the return type for coroutines that perform I/O
    and return a value of type `T`. The coroutine body does not start
    executing until the task is awaited, enabling efficient composition
    without unnecessary eager execution.

    The task participates in the I/O awaitable protocol: when awaited,
    it receives the caller's executor and stop token, propagating them
    to nested `co_await` expressions. This enables cancellation and
    proper completion dispatch across executor boundaries.

    @tparam T The result type. Use `task<>` for `task<void>`.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Unsafe.

    @par Example

    @code
    task<int> compute_value()
    {
        auto [ec, n] = co_await stream.read_some( buf );
        if( ec )
            co_return 0;
        co_return process( buf, n );
    }

    task<> run_session( tcp_socket sock )
    {
        int result = co_await compute_value();
        // ...
    }
    @endcode

    @see IoLaunchableTask, IoAwaitableTask, run, run_async
*/
template<typename T = void>
struct [[nodiscard]] BOOST_CAPY_CORO_AWAIT_ELIDABLE
    task
{
    struct promise_type
        : io_awaitable_support<promise_type>
        , detail::task_return_base<T>
    {
    private:
        friend task;
        union { std::exception_ptr ep_; };
        bool has_ep_;

    public:
        promise_type() noexcept
            : has_ep_(false)
        {
        }

        ~promise_type()
        {
            if(has_ep_)
                ep_.~exception_ptr();
        }

        std::exception_ptr exception() const noexcept
        {
            if(has_ep_)
                return ep_;
            return {};
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
                    auto* fa = p_->frame_allocator();
                    if(fa && fa != current_frame_allocator())
                        current_frame_allocator() = fa;
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
            new (&ep_) std::exception_ptr(std::current_exception());
            has_ep_ = true;
        }

        template<class Awaitable>
        struct transform_awaiter
        {
            std::decay_t<Awaitable> a_;
            promise_type* p_;

            bool await_ready() noexcept
            {
                return a_.await_ready();
            }

            decltype(auto) await_resume()
            {
                // Restore TLS before body resumes
                auto* fa = p_->frame_allocator();
                if(fa && fa != current_frame_allocator())
                    current_frame_allocator() = fa;
                return a_.await_resume();
            }

            template<class Promise>
            auto await_suspend(std::coroutine_handle<Promise> h) noexcept
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

    /// Destroy the task and its coroutine frame if owned.
    ~task()
    {
        if(h_)
            h_.destroy();
    }

    /// Return false; tasks are never immediately ready.
    bool await_ready() const noexcept
    {
        return false;
    }

    /// Return the result or rethrow any stored exception.
    auto await_resume()
    {
        if(h_.promise().has_ep_)
            std::rethrow_exception(h_.promise().ep_);
        if constexpr (! std::is_void_v<T>)
            return std::move(*h_.promise().result_);
        else
            return;
    }

    /// Start execution with the caller's context.
    coro await_suspend(coro cont,
        executor_ref const& caller_ex, std::stop_token const& token)
    {
        h_.promise().set_continuation(cont, caller_ex);
        h_.promise().set_executor(caller_ex);
        h_.promise().set_stop_token(token);
        return h_;
    }

    /// Return the coroutine handle.
    std::coroutine_handle<promise_type> handle() const noexcept
    {
        return h_;
    }

    /** Release ownership of the coroutine frame.

        After calling this, destroying the task does not destroy the
        coroutine frame. The caller becomes responsible for the frame's
        lifetime.

        @par Postconditions
        `handle()` returns the original handle, but the task no longer
        owns it.
    */
    void release() noexcept
    {
        h_ = nullptr;
    }

    task(task const&) = delete;
    task& operator=(task const&) = delete;

    /// Move construct, transferring ownership.
    task(task&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    /// Move assign, transferring ownership.
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
