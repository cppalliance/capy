//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_STOP_TOKEN_SUPPORT_HPP
#define BOOST_CAPY_EX_STOP_TOKEN_SUPPORT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/any_coro.hpp>
#include <boost/capy/ex/get_stop_token.hpp>

#include <coroutine>
#include <stop_token>
#include <type_traits>

namespace boost {
namespace capy {

/** CRTP mixin that adds stop token support to a promise type.

    Inherit from this class to enable two capabilities in your coroutine:

    1. **Stop token storage** — The mixin stores the `std::stop_token`
       that was passed when your coroutine was awaited.

    2. **Stop token access** — Coroutine code can retrieve the token via
       `co_await get_stop_token()`.

    @tparam Derived The derived promise type (CRTP pattern).

    @par Basic Usage

    For coroutines that only need to access their stop token:

    @code
    struct my_task
    {
        struct promise_type : stop_token_support<promise_type>
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
        // Use token...
    }
    @endcode

    @par Custom Awaitable Transformation

    If your promise needs to transform awaitables (e.g., for affinity or
    logging), override `transform_awaitable` instead of `await_transform`:

    @code
    struct promise_type : stop_token_support<promise_type>
    {
        any_executor_ref ex_;

        template<typename A>
        auto transform_awaitable(A&& a)
        {
            // Your custom transformation logic
            return make_affine(std::forward<A>(a), ex_);
        }
    };
    @endcode

    The mixin's `await_transform` intercepts @ref get_stop_token_tag and
    delegates all other awaitables to your `transform_awaitable`.

    @par Making Your Coroutine Stoppable

    The mixin handles the "inside the coroutine" part—accessing the token.
    To receive a token when your coroutine is awaited (satisfying
    @ref IoAwaitable), implement the stoppable `await_suspend`
    overload on your coroutine return type:

    @code
    struct my_task
    {
        struct promise_type : stop_token_support<promise_type> { ... };

        std::coroutine_handle<promise_type> h_;

        // Stoppable await_suspend receives and stores the token
        template<class Ex>
        any_coro await_suspend(any_coro cont, Ex const& ex, std::stop_token token)
        {
            h_.promise().set_stop_token(token);  // Store via mixin API
            // ... rest of suspend logic ...
        }
    };
    @endcode

    @par Thread Safety
    The stop token is stored during `await_suspend` and read during
    `co_await get_stop_token()`. These occur on the same logical thread
    of execution, so no synchronization is required.

    @see get_stop_token
    @see get_stop_token_tag
    @see IoAwaitable
*/
template<typename Derived>
class stop_token_support
{
    std::stop_token stop_token_;

public:
    /** Store a stop token for later retrieval.

        Call this from your coroutine type's stoppable `await_suspend`
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

        This function handles @ref get_stop_token_tag specially, returning
        an awaiter that yields the stored stop token. All other awaitables
        are delegated to @ref transform_awaitable.

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
