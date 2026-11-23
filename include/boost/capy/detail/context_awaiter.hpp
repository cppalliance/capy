//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_CONTEXT_AWAITER_HPP
#define BOOST_CAPY_DETAIL_CONTEXT_AWAITER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/concept/context_awaitable.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/frame_allocator.hpp>

#include <coroutine>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

/** Pass a coroutine's environment to an awaitable it awaits.

    The compiler calls `await_suspend` with the suspending coroutine's
    handle only, but a @ref ContextAwaitable also needs that
    coroutine's @ref await_context. A promise supplies it by returning
    this from `await_transform`: the awaiter forwards the environment
    to the wrapped awaitable's `await_suspend(h, env)`, and reinstalls
    the environment's frame allocator before the coroutine continues,
    since it may resume on another thread.

    @ref context_awaitable_promise_base does this for every
    @ref ContextAwaitable by default, and `task` uses it the same way.

    The awaiter also applies the workarounds Capy needs for compilers
    that miscompile symmetric transfer, so code using it does not have
    to.

    The awaitable is moved or copied into the awaiter, which awaits
    its own copy. The environment is read from the promise each time
    the awaiter suspends and resumes, so it is the one in place at that
    moment.

    @par Exception Safety
    An exception from the wrapped awaitable's `await_ready` or
    `await_suspend` calls `std::terminate`, as it does in a @ref task.
    A promise that reports such an exception as the coroutine's
    failure, as `when_all` does, needs its own adapter.
*/
template<ContextAwaitable A, class P>
struct context_awaiter
{
    // An aggregate on purpose: with a user-provided constructor, clang's
    // static analyzer reports the promise's environment as uninitialized.

    /// The awaitable, stored by value.
    A a_;

    /// The awaiting coroutine's promise.
    P* p_;

    /** Check whether the awaitable is already complete.

        Where the symmetric-transfer workaround is on, this always
        returns `false` and @ref await_suspend asks the awaitable
        instead.

        @note An exception from the awaitable's `await_ready` calls
        `std::terminate`.

        @return `true` if the coroutine need not suspend.
    */
    bool await_ready() noexcept
    {
#if BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND
        return false;
#else
        return a_.await_ready();
#endif
    }

    /** Suspend by calling the awaitable with the environment.

        Where the symmetric-transfer workaround is on, the result is a
        `bool` instead of the awaitable's own suspension decision.

        @note An exception from the awaitable's `await_suspend` calls
        `std::terminate`.

        @param h The coroutine performing the `co_await`.

        @return The awaitable's own suspension decision.
    */
    template<class Promise>
    auto await_suspend(std::coroutine_handle<Promise> h) noexcept
    {
        await_context const* const env = p_->environment();
#if BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND
        return detail::await_ready_then_suspend(a_, h, env);
#else
        using R = decltype(a_.await_suspend(h, env));
        if constexpr (std::is_same_v<R, std::coroutine_handle<>>)
            return detail::symmetric_transfer(a_.await_suspend(h, env));
        else
            return a_.await_suspend(h, env);
#endif
    }

    /** Reinstall the frame allocator and return the awaitable's result.

        @return The awaitable's `await_resume` result, unchanged.
    */
    decltype(auto) await_resume()
    {
        set_current_frame_allocator(p_->environment()->frame_allocator);
        return a_.await_resume();
    }
};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
