//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_AWAITABLE_HPP
#define BOOST_CAPY_IO_AWAITABLE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/any_coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <coroutine>
#include <stop_token>
#include <type_traits>

namespace boost {
namespace capy {

/** Tag type for coroutine stop token retrieval.

    This tag is returned by @ref get_stop_token and intercepted by a
    promise type's `await_transform` to yield the coroutine's current
    stop token. The tag itself carries no data; it serves only as a
    sentinel for compile-time dispatch.

    @see get_stop_token
    @see io_awaitable_support
*/
struct get_stop_token_tag {};

/** Tag type for coroutine executor retrieval.

    This tag is returned by @ref get_executor and intercepted by a
    promise type's `await_transform` to yield the coroutine's current
    executor. The tag itself carries no data; it serves only as a
    sentinel for compile-time dispatch.

    @see get_executor
    @see io_awaitable_support
*/
struct get_executor_tag {};

/** Return a tag that yields the current stop token when awaited.

    Use `co_await get_stop_token()` inside a coroutine whose promise
    type supports stop token access (e.g., inherits from
    @ref io_awaitable_support). The returned stop token reflects whatever
    token was passed to this coroutine when it was awaited.

    @par Example
    @code
    task<void> cancellable_work()
    {
        auto token = co_await get_stop_token();
        for (int i = 0; i < 1000; ++i)
        {
            if (token.stop_requested())
                co_return;  // Exit gracefully on cancellation
            co_await process_chunk(i);
        }
    }
    @endcode

    @par Behavior
    @li If no stop token was propagated, returns a default-constructed
        `std::stop_token` (where `stop_possible()` returns `false`).
    @li The returned token remains valid for the coroutine's lifetime.
    @li This operation never suspends; `await_ready()` always returns `true`.

    @return A tag that `await_transform` intercepts to return the stop token.

    @see get_stop_token_tag
    @see io_awaitable_support
*/
inline get_stop_token_tag get_stop_token() noexcept
{
    return {};
}

/** Return a tag that yields the current executor when awaited.

    Use `co_await get_executor()` inside a coroutine whose promise
    type supports executor access (e.g., inherits from
    @ref io_awaitable_support). The returned executor reflects the
    executor this coroutine is bound to.

    @par Example
    @code
    task<void> example()
    {
        executor_ref ex = co_await get_executor();
        // ex is the executor this coroutine is bound to
    }
    @endcode

    @par Behavior
    @li If no executor was set, returns a default-constructed
        `executor_ref` (where `operator bool()` returns `false`).
    @li This operation never suspends; `await_ready()` always returns `true`.

    @return A tag that `await_transform` intercepts to return the executor.

    @see get_executor_tag
    @see io_awaitable_support
*/
inline get_executor_tag get_executor() noexcept
{
    return {};
}

/** Concept for I/O awaitable types.

    An awaitable is an I/O awaitable if it participates in the I/O awaitable
    protocol by accepting an executor and a stop_token in its `await_suspend`
    method. This enables zero-overhead scheduler affinity and cancellation
    support.

    @tparam A The awaitable type.
    @tparam P The promise type (defaults to void).

    @par Requirements
    @li `A` must provide `await_suspend(std::coroutine_handle<P> h, Ex const& ex,
        std::stop_token token)`
    @li The awaitable must use the executor `ex` to resume the caller
    @li The awaitable should use the stop_token to support cancellation

    @par Example
    @code
    struct my_io_op
    {
        template<typename Executor>
        auto await_suspend(std::coroutine_handle<> h, Executor const& ex,
            std::stop_token token)
        {
            start_async([h, &ex, token] {
                if (token.stop_requested()) {
                    // Handle cancellation
                }
                ex.dispatch(h);  // Schedule resumption through executor
            });
            return std::noop_coroutine();
        }
        // ... await_ready, await_resume ...
    };
    @endcode
*/
template<typename A, typename P = void>
concept IoAwaitable =
    requires(
        A a,
        std::coroutine_handle<P> h,
        executor_ref ex,
        std::stop_token token)
    {
        a.await_suspend(h, ex, token);
    };

/** CRTP mixin that adds I/O awaitable support to a promise type.

    Inherit from this class to enable these capabilities in your coroutine:

    1. **Stop token storage** — The mixin stores the `std::stop_token`
       that was passed when your coroutine was awaited.

    2. **Stop token access** — Coroutine code can retrieve the token via
       `co_await get_stop_token()`.

    3. **Executor storage** — The mixin stores the `executor_ref`
       that this coroutine is bound to.

    4. **Executor access** — Coroutine code can retrieve the executor via
       `co_await get_executor()`.

    @tparam Derived The derived promise type (CRTP pattern).

    @par Basic Usage

    For coroutines that need to access their stop token or executor:

    @code
    struct my_task
    {
        struct promise_type : io_awaitable_support<promise_type>
        {
            my_task get_return_object();
            std::suspend_always initial_suspend() noexcept;
            std::suspend_always final_suspend() noexcept;
            void return_void();
            void unhandled_exception();
        };

        // ... awaitable interface ...
    };

    my_task example()
    {
        auto token = co_await get_stop_token();
        auto ex = co_await get_executor();
        // Use token and ex...
    }
    @endcode

    @par Custom Awaitable Transformation

    If your promise needs to transform awaitables (e.g., for affinity or
    logging), override `transform_awaitable` instead of `await_transform`:

    @code
    struct promise_type : io_awaitable_support<promise_type>
    {
        template<typename A>
        auto transform_awaitable(A&& a)
        {
            // Your custom transformation logic
            return std::forward<A>(a);
        }
    };
    @endcode

    The mixin's `await_transform` intercepts @ref get_stop_token_tag and
    @ref get_executor_tag, then delegates all other awaitables to your
    `transform_awaitable`.

    @par Making Your Coroutine an IoAwaitable

    The mixin handles the "inside the coroutine" part—accessing the token
    and executor. To receive these when your coroutine is awaited (satisfying
    @ref IoAwaitable), implement the `await_suspend` overload on your
    coroutine return type:

    @code
    struct my_task
    {
        struct promise_type : io_awaitable_support<promise_type> { ... };

        std::coroutine_handle<promise_type> h_;

        // IoAwaitable await_suspend receives and stores the token and executor
        template<class Ex>
        any_coro await_suspend(any_coro cont, Ex const& ex, std::stop_token token)
        {
            h_.promise().set_stop_token(token);
            h_.promise().set_executor(ex);
            // ... rest of suspend logic ...
        }
    };
    @endcode

    @par Thread Safety
    The stop token and executor are stored during `await_suspend` and read
    during `co_await get_stop_token()` or `co_await get_executor()`. These
    occur on the same logical thread of execution, so no synchronization
    is required.

    @see get_stop_token
    @see get_executor
    @see IoAwaitable
*/
template<typename Derived>
class io_awaitable_support
{
    std::stop_token stop_token_;
    executor_ref ex_;

public:
    /** Store a stop token for later retrieval.

        Call this from your coroutine type's `await_suspend`
        overload to make the token available via `co_await get_stop_token()`.

        @param token The stop token to store.
    */
    void set_stop_token(std::stop_token token) noexcept
    {
        stop_token_ = token;
    }

    /** Return the stored stop token.

        @return The stop token, or a default-constructed token if none was set.
    */
    std::stop_token const& stop_token() const noexcept
    {
        return stop_token_;
    }

    /** Store an executor for later retrieval.

        Call this from your coroutine type's `await_suspend`
        overload to make the executor available via `co_await get_executor()`.

        @param ex The executor to store.
    */
    void set_executor(executor_ref ex) noexcept
    {
        ex_ = ex;
    }

    /** Return the stored executor.

        @return The executor, or a default-constructed executor_ref if none was set.
    */
    executor_ref executor() const noexcept
    {
        return ex_;
    }

    /** Transform an awaitable before co_await.

        Override this in your derived promise type to customize how
        awaitables are transformed. The default implementation passes
        the awaitable through unchanged.

        @param a The awaitable expression from `co_await a`.

        @return The transformed awaitable.
    */
    template<typename A>
    decltype(auto) transform_awaitable(A&& a)
    {
        return std::forward<A>(a);
    }

    /** Intercept co_await expressions.

        This function handles @ref get_stop_token_tag and @ref get_executor_tag
        specially, returning an awaiter that yields the stored value. All other
        awaitables are delegated to @ref transform_awaitable.

        @param t The awaited expression.

        @return An awaiter for the expression.
    */
    template<typename T>
    auto await_transform(T&& t)
    {
        if constexpr (std::is_same_v<std::decay_t<T>, get_stop_token_tag>)
        {
            struct awaiter
            {
                std::stop_token token_;

                bool await_ready() const noexcept
                {
                    return true;
                }

                void await_suspend(any_coro) const noexcept
                {
                }

                std::stop_token await_resume() const noexcept
                {
                    return token_;
                }
            };
            return awaiter{stop_token_};
        }
        else if constexpr (std::is_same_v<std::decay_t<T>, get_executor_tag>)
        {
            struct awaiter
            {
                executor_ref ex_;

                bool await_ready() const noexcept
                {
                    return true;
                }

                void await_suspend(any_coro) const noexcept
                {
                }

                executor_ref await_resume() const noexcept
                {
                    return ex_;
                }
            };
            return awaiter{ex_};
        }
        else
        {
            return static_cast<Derived*>(this)->transform_awaitable(
                std::forward<T>(t));
        }
    }
};

} // namespace capy
} // namespace boost

#endif
