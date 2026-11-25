//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
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

/** Provide awaitable accessors for the running coroutine's environment.

    The `this_coro` namespace contains tag objects that can be awaited
    to retrieve information about the current coroutine's execution
    context. These tags are intercepted by a promise type's
    `await_transform` to yield the appropriate values without suspending.

    @par Example
    @par !example example


    @see @ref context_awaitable_promise_base, @ref await_context
*/
namespace this_coro {

/** Selects `co_await this_coro::environment` to fetch the running coroutine's environment.

    This tag is intercepted by a promise type's `await_transform` to
    yield the coroutine's current execution environment. The tag itself
    carries no data; it serves only as a sentinel for compile-time dispatch.

    @see @ref environment
    @see @ref context_awaitable_promise_base
*/
struct environment_tag {};

/** Selects `co_await this_coro::executor` to fetch the running coroutine's executor.

    This tag is intercepted by a promise type's `await_transform` to
    yield the coroutine's current executor. The tag itself carries no
    data; it serves only as a sentinel for compile-time dispatch.

    @see @ref executor
    @see @ref context_awaitable_promise_base
*/
struct executor_tag {};

/** Selects `co_await this_coro::stop_token` to fetch the running coroutine's stop token.

    This tag is intercepted by a promise type's `await_transform` to
    yield the coroutine's current stop token. The tag itself carries
    no data; it serves only as a sentinel for compile-time dispatch.

    @see @ref stop_token
    @see @ref context_awaitable_promise_base
*/
struct stop_token_tag {};

/** Selects `co_await this_coro::frame_allocator` to fetch the running coroutine's frame allocator.

    This tag is intercepted by a promise type's `await_transform` to
    yield the coroutine's current frame allocator. The tag itself carries
    no data; it serves only as a sentinel for compile-time dispatch.

    @see @ref frame_allocator
    @see @ref context_awaitable_promise_base
*/
struct frame_allocator_tag {};

/** Yield the running coroutine's environment when awaited.

    Use `co_await this_coro::environment` inside a coroutine whose promise
    type supports environment access (e.g., inherits from
    @ref context_awaitable_promise_base). The returned environment contains the
    executor, stop token, and allocator for this coroutine.

    @pre An `await_context` has been installed for this coroutine
    before the tag is awaited. Starting the coroutine via @ref run or
    @ref run_async installs one. Awaiting the tag without an installed
    environment is undefined behavior (an assertion fires in debug
    builds).

    @par Behavior
    @li Returns a pointer to the stored `await_context`
    @li This operation never suspends; `await_ready()` always returns `true`

    @par Example
    @par !example example


    @see @ref environment_tag
    @see @ref context_awaitable_promise_base
    @see @ref await_context
*/
inline constexpr environment_tag environment{};

/** Yield the running coroutine's executor when awaited.

    Use `co_await this_coro::executor` inside a coroutine whose promise
    type supports executor access (e.g., inherits from
    @ref context_awaitable_promise_base). The returned executor reflects the
    executor this coroutine is bound to.

    @pre An `await_context` has been installed for this coroutine
    before the tag is awaited (see @ref environment). Awaiting it
    without an installed environment is undefined behavior (an
    assertion fires in debug builds).

    @par Behavior
    @li Returns the installed environment's `executor` field. If the started
        chain installed an `await_context` whose `executor` was left default, the
        result is a default-constructed `executor_ref` (where `operator bool()`
        returns `false`).
    @li This operation never suspends; `await_ready()` always returns `true`.

    @par Example
    @par !example example


    @see @ref executor_tag
    @see @ref context_awaitable_promise_base
*/
inline constexpr executor_tag executor{};

/** Yield the running coroutine's stop token when awaited.

    Use `co_await this_coro::stop_token` inside a coroutine whose promise
    type supports stop token access (e.g., inherits from
    @ref context_awaitable_promise_base). The returned stop token is the stop
    token in the coroutine's installed environment.

    @pre An `await_context` has been installed for this coroutine
    before the tag is awaited (see @ref environment). Awaiting it
    without an installed environment is undefined behavior (an
    assertion fires in debug builds).

    @par Behavior
    @li Returns the installed environment's `stop_token` field. If the started
        chain installed an `await_context` whose `stop_token` was left default, the
        result is a default-constructed `std::stop_token` (where
        `stop_possible()` returns `false`).
    @li The returned token remains valid for the coroutine's lifetime.
    @li This operation never suspends; `await_ready()` always returns `true`.

    @par Example
    @par !example example


    @see @ref stop_token_tag
    @see @ref context_awaitable_promise_base
*/
inline constexpr stop_token_tag stop_token{};

/** Yield the running coroutine's frame allocator when awaited.

    Use `co_await this_coro::frame_allocator` inside a coroutine whose promise
    type supports frame allocator access (e.g., inherits from
    @ref context_awaitable_promise_base). The returned pointer is the
    frame allocator in the coroutine's installed environment. It can
    differ from the resource that allocated the running frame.

    @pre An `await_context` has been installed for this coroutine
    before the tag is awaited (see @ref environment). Awaiting it
    without an installed environment is undefined behavior (an
    assertion fires in debug builds).

    @par Behavior
    @li Returns the installed environment's `frame_allocator` field.
        Environments installed by @ref run_async carry a non-null pointer.
        @ref run installs the resource it was given, or else the caller's
        pointer, which may be null.
    @li This operation never suspends; `await_ready()` always returns `true`.

    @par Example
    @par !example example


    @see @ref frame_allocator_tag
    @see @ref context_awaitable_promise_base
*/
inline constexpr frame_allocator_tag frame_allocator{};

} // namespace this_coro
} // namespace capy
} // namespace boost

#endif
