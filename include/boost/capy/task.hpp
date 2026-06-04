//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TASK_HPP
#define BOOST_CAPY_TASK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/io_awaitable_promise_base.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>

#include <concepts>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace boost {
namespace capy {

template<typename ... Ts>
struct io_result;

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

template<typename ... Us>
void handle_yield_result(io_result<Us...> & res, std::error_code ec)
{
    static_assert((std::constructible_from<Us> && ...), "co_yield requires all result value to be default constructible");
}

} // namespace detail

/** Lazy coroutine task satisfying @ref IoRunnable.

    Use `task<T>` as the return type for coroutines that perform I/O
    and return a value of type `T`. The coroutine body does not start
    executing until the task is awaited, enabling efficient composition
    without unnecessary eager execution.

    The task participates in the I/O awaitable protocol: when awaited,
    it receives the caller's executor and stop token, propagating them
    to nested `co_await` expressions. This enables cancellation and
    proper completion dispatch across executor boundaries.

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

    @tparam T The result type. Use `task<>` for `task<void>`.

    @see IoRunnable, IoAwaitable, run, run_async
*/
template<typename T = void>
struct [[nodiscard]] BOOST_CAPY_CORO_AWAIT_ELIDABLE
    task
{
    struct promise_type
        : io_awaitable_promise_base<promise_type>
        , detail::task_return_base<T>
    {
    protected:
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

                void await_suspend(std::coroutine_handle<>) const noexcept
                {
                }

                void await_resume() const noexcept
                {
                    // Restore TLS when body starts executing
                    set_current_frame_allocator(p_->environment()->frame_allocator);
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

                std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept
                {
                    return p_->continuation();
                }

                void await_resume() const noexcept
                {
                }
            };
            return awaiter{this};
        }

        void unhandled_exception() noexcept
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
                set_current_frame_allocator(p_->environment()->frame_allocator);
                return a_.await_resume();
            }

            template<class Promise>
            auto await_suspend(std::coroutine_handle<Promise> h) noexcept
            {
                using R = decltype(a_.await_suspend(h, p_->environment()));
                if constexpr (std::is_same_v<R, std::coroutine_handle<>>)
                    return detail::symmetric_transfer(a_.await_suspend(h, p_->environment()));
                else
                    return a_.await_suspend(h, p_->environment());
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

                

        template<class ... Ts>
        auto yield_value(io_result<Ts...> res)
        {
            struct awaitable
            {
                io_result<Ts...> res;
                
                bool await_ready() const {return !res.ec;}
                void await_suspend(std::coroutine_handle<promise_type> h)
                {
                    auto & p = h.promise();
                    try 
                    {
                        detail::handle_yield_result(h.promise(), res.ec);
                    }
                    catch (...)
                    {
                        p.uncaught_exception();
                    }
                    
                }
                std::tuple<Ts...> await_resume() 
                {
                    return std::move(res.values);
                }
            };
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
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont, io_env const* env)
    {
        h_.promise().set_continuation(cont);
        h_.promise().set_environment(env);
        return h_;
    }

    /** Return the coroutine handle.

        @note Do not call `destroy()` on the returned handle while the
        task is being awaited. The task's lifetime is normally managed
        by `run_async`, `run`, or the awaiting parent; manually
        destroying a suspended task that another coroutine is awaiting
        produces undefined behavior. For cooperative cancellation, use
        `std::stop_token`.

        @return The coroutine handle.
    */
    std::coroutine_handle<promise_type> handle() const noexcept
    {
        return h_;
    }

    /** Release ownership of the coroutine frame.

        After calling this, destroying the task does not destroy the
        coroutine frame. The caller becomes responsible for the frame's
        lifetime.

        @note If the caller intends to call `destroy()` on the
        released handle, it must do so only when the task has not
        started or has fully completed. Destroying a suspended task
        that is being awaited produces undefined behavior.

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

    /// Construct by moving, transferring ownership.
    task(task&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    /// Assign by moving, transferring ownership.
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
