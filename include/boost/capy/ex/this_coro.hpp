//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_THIS_CORO_HPP
#define BOOST_CAPY_EX_THIS_CORO_HPP

#include <boost/capy/detail/config.hpp>

namespace boost {
namespace capy {

/** Namespace for coroutine environment accessors.

    The `this_coro` namespace contains tag objects that can be awaited
    to retrieve information about the current coroutine's execution
    context. These tags are intercepted by a promise type's
    `await_transform` to yield the appropriate values without suspending.

    @par Example
    @code
    task<void> example()
    {
        auto const& env = co_await this_coro::environment;
        auto ex = co_await this_coro::executor;
        auto token = co_await this_coro::stop_token;
        auto* alloc = co_await this_coro::allocator;
    }
    @endcode

    @see io_awaitable_promise_base, io_env
*/
namespace this_coro {

/** Tag type for coroutine environment retrieval.

    This tag is intercepted by a promise type's `await_transform` to
    yield the coroutine's current execution environment. The tag itself
    carries no data; it serves only as a sentinel for compile-time dispatch.

    @see environment
    @see io_awaitable_promise_base
*/
struct environment_tag {};

/** Tag type for coroutine executor retrieval.

    This tag is intercepted by a promise type's `await_transform` to
    yield the coroutine's current executor. The tag itself carries no
    data; it serves only as a sentinel for compile-time dispatch.

    @see executor
    @see io_awaitable_promise_base
*/
struct executor_tag {};

/** Tag type for coroutine stop token retrieval.

    This tag is intercepted by a promise type's `await_transform` to
    yield the coroutine's current stop token. The tag itself carries
    no data; it serves only as a sentinel for compile-time dispatch.

    @see stop_token
    @see io_awaitable_promise_base
*/
struct stop_token_tag {};

/** Tag type for coroutine allocator retrieval.

    This tag is intercepted by a promise type's `await_transform` to
    yield the coroutine's current frame allocator. The tag itself carries
    no data; it serves only as a sentinel for compile-time dispatch.

    @see allocator
    @see io_awaitable_promise_base
*/
struct allocator_tag {};

/** Tag object that yields the current environment when awaited.

    Use `co_await this_coro::environment` inside a coroutine whose promise
    type supports environment access (e.g., inherits from
    @ref io_awaitable_promise_base). The returned environment contains the
    executor, stop token, and allocator for this coroutine.

    @par Example
    @code
    task<void> example()
    {
        auto const& env = co_await this_coro::environment;
        // env.executor - the executor this coroutine is bound to
        // env.stop_token - the stop token for cancellation
        // env.allocator - the allocator for frame allocation
    }
    @endcode

    @par Behavior
    @li Returns a const reference to the stored `io_env`
    @li This operation never suspends; `await_ready()` always returns `true`

    @see environment_tag
    @see io_awaitable_promise_base
    @see io_env
*/
inline constexpr environment_tag environment{};

/** Tag object that yields the current executor when awaited.

    Use `co_await this_coro::executor` inside a coroutine whose promise
    type supports executor access (e.g., inherits from
    @ref io_awaitable_promise_base). The returned executor reflects the
    executor this coroutine is bound to.

    @par Example
    @code
    task<void> example()
    {
        executor_ref ex = co_await this_coro::executor;
    }
    @endcode

    @par Behavior
    @li If no executor was set, returns a default-constructed
        `executor_ref` (where `operator bool()` returns `false`).
    @li This operation never suspends; `await_ready()` always returns `true`.

    @see executor_tag
    @see io_awaitable_promise_base
*/
inline constexpr executor_tag executor{};

/** Tag object that yields the current stop token when awaited.

    Use `co_await this_coro::stop_token` inside a coroutine whose promise
    type supports stop token access (e.g., inherits from
    @ref io_awaitable_promise_base). The returned stop token reflects whatever
    token was passed to this coroutine when it was awaited.

    @par Example
    @code
    task<void> cancellable_work()
    {
        auto token = co_await this_coro::stop_token;
        if (token.stop_requested())
            co_return;
    }
    @endcode

    @par Behavior
    @li If no stop token was propagated, returns a default-constructed
        `std::stop_token` (where `stop_possible()` returns `false`).
    @li The returned token remains valid for the coroutine's lifetime.
    @li This operation never suspends; `await_ready()` always returns `true`.

    @see stop_token_tag
    @see io_awaitable_promise_base
*/
inline constexpr stop_token_tag stop_token{};

/** Tag object that yields the current frame allocator when awaited.

    Use `co_await this_coro::allocator` inside a coroutine whose promise
    type supports allocator access (e.g., inherits from
    @ref io_awaitable_promise_base). The returned pointer is the memory resource
    used for coroutine frame allocation.

    @par Example
    @code
    task<void> example()
    {
        auto* alloc = co_await this_coro::allocator;
        // alloc is nullptr when using the default allocator
    }
    @endcode

    @par Behavior
    @li Returns `nullptr` when the default allocator is in use.
    @li This operation never suspends; `await_ready()` always returns `true`.

    @see allocator_tag
    @see io_awaitable_promise_base
*/
inline constexpr allocator_tag allocator{};

} // namespace this_coro
} // namespace capy
} // namespace boost

#endif
