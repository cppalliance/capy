//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_CONTEXT_AWAITABLE_PROMISE_BASE_HPP
#define BOOST_CAPY_EX_CONTEXT_AWAITABLE_PROMISE_BASE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/detail/context_awaiter.hpp>
#include <boost/capy/ex/frame_alloc_mixin.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/this_coro.hpp>

#include <coroutine>
#include <memory_resource>
#include <stop_token>
#include <type_traits>

namespace boost {
namespace capy {

/** Give a promise frame allocation and environment propagation to I/O awaitables.

    Inherit from this class to enable these capabilities in your coroutine:

    1. **Frame allocation**: the mixin provides `operator new/delete` that
       use the current frame allocator (see
       @ref get_current_frame_allocator).

    2. **Environment storage** — The mixin stores a pointer to the `await_context`
       containing the executor, stop token, and allocator for this coroutine.

    3. **Environment access** — Coroutine code can retrieve the environment
       via `co_await this_coro::environment`, or individual fields via
       `co_await this_coro::executor`, `co_await this_coro::stop_token`,
       and `co_await this_coro::frame_allocator`.

    4. **Environment propagation** — Each @ref ContextAwaitable the
       coroutine awaits receives that environment.

    @par Basic Usage

    For coroutines that need to access their execution environment:

    @par !example example_1


    @par Custom Awaitable Transformation

    If your promise needs to transform awaitables (e.g., for affinity or
    logging), override `transform_awaitable` instead of `await_transform`.
    Hand @ref ContextAwaitable types on to the base's
    `transform_awaitable` so they still receive the environment:

    @par !example example_2


    The mixin's `await_transform` intercepts @ref this_coro::environment_tag
    and the fine-grained tag types (@ref this_coro::executor_tag,
    @ref this_coro::stop_token_tag, @ref this_coro::frame_allocator_tag),
    then delegates all other awaitables to your `transform_awaitable`.

    @par Making Your Coroutine a ContextAwaitable

    The mixin handles the "inside the coroutine" part—accessing the
    environment. To receive the environment when your coroutine is awaited
    (satisfying @ref ContextAwaitable), implement the `await_suspend` overload
    on your coroutine return type:

    @par !example example_3


    @par Ownership
    The object that awaits a coroutine, such as the parent's `task`,
    owns its frame. A chain is therefore destroyed from its top down,
    each frame freeing the one it awaits. The suspended coroutine at the
    bottom is held by whoever will resume it: an operation, queue or
    primitive. A holder that will never resume it calls @ref abandon,
    which unwinds the chain's segment from its owner (see
    @ref chain_owner) rather than destroying the bottom frame.
    Destroying a frame that is still awaited, other than in that
    unwind, calls `std::terminate`.

    @par Thread Safety
    The environment is stored before the coroutine's body runs, from
    `await_suspend` or the promise's constructor, and read during
    `co_await this_coro::environment`. These occur on the same logical
    thread of execution, so no synchronization is required.

    @note A chain waiting on a primitive stored in its own frames owns
    itself through that primitive. Only a stop request or a signal on
    the primitive frees it.

    @note A mutex must outlive every coroutine holding or handed its
    lock.

    @note A cross-thread `wake()` racing the teardown of an armed
    waker's frame on its executor thread is a precondition violation.

    @tparam Derived The derived promise type (CRTP pattern).

    @see @ref this_coro::environment, @ref this_coro::executor,
         @ref this_coro::stop_token, @ref this_coro::frame_allocator
    @see @ref await_context
    @see @ref ContextAwaitable
*/
template<typename Derived>
class context_awaitable_promise_base
    : public frame_alloc_mixin
{
    await_context const* env_ = nullptr;
    mutable std::coroutine_handle<> cont_{std::noop_coroutine()};

public:
    /** Store the continuation to resume on completion.

        Call this from your coroutine type's `await_suspend` overload
        to set up the completion path. Your `final_suspend` awaiter
        should return `continuation()` by symmetric transfer.

        @param cont The continuation to resume on completion.
    */
    void set_continuation(std::coroutine_handle<> cont) noexcept
    {
        cont_ = cont;
    }

    /** Return and consume the stored continuation handle.

        Resets the stored handle to `std::noop_coroutine()`, so a second
        call returns a no-op handle.

        @return The continuation for symmetric transfer.
    */
    std::coroutine_handle<> continuation() const noexcept
    {
        return std::exchange(cont_, std::noop_coroutine());
    }

    /** Store a pointer to the execution environment.

        Call this before the coroutine's body runs. The environment then
        reaches `co_await this_coro::environment` and each awaited
        @ref ContextAwaitable. Call it from your coroutine type's
        `await_suspend` overload when it receives the environment from
        the awaiting coroutine. Call it from the promise's constructor
        when the promise holds its own. The pointed-to `await_context`
        must outlive this coroutine.

        @param env The environment to store.
    */
    void set_environment(await_context const* env) noexcept
    {
        env_ = env;
    }

    /** Return the stored execution environment.

        @pre @ref set_environment has been called with a non-null
        pointer.

        @return The environment.
    */
    await_context const* environment() const noexcept
    {
        BOOST_CAPY_ASSERT(env_);
        return env_;
    }

    /** Transform an awaitable before co_await.

        The default wraps a @ref ContextAwaitable so that it receives
        this coroutine's environment when the coroutine suspends on it.
        The wrapper also reinstalls the frame allocator when the
        coroutine resumes. The awaitable is moved or copied into the
        wrapper, so an lvalue is awaited as a copy. A move-only
        awaitable must be passed as an rvalue. The wrapper also works
        around compilers that miscompile
        symmetric transfer, so a promise relying on the default need
        not. Any other awaitable passes through unchanged. Override
        this in your derived promise type to customize how awaitables
        are transformed; an override that still awaits
        @ref ContextAwaitable types passes them to this function.

        @pre @ref set_environment has been called, if `A` satisfies
        @ref ContextAwaitable. A promise that receives its environment
        when awaited calls it from its `await_suspend`, before the body
        runs. A promise that holds its own environment calls it from
        its constructor.

        @note An exception from a wrapped awaitable's `await_ready` or
        `await_suspend` calls `std::terminate`.

        @param a The awaitable expression from `co_await a`.

        @return The transformed awaitable.
    */
    template<typename A>
    decltype(auto) transform_awaitable(A&& a)
    {
        if constexpr (ContextAwaitable<std::decay_t<A>>)
            return detail::context_awaiter<
                std::decay_t<A>, context_awaitable_promise_base>{
                    std::forward<A>(a), this};
        else
            return std::forward<A>(a);
    }

    /** Intercept co_await expressions.

        This function handles @ref this_coro::environment_tag and
        the fine-grained tags (@ref this_coro::executor_tag,
        @ref this_coro::stop_token_tag, @ref this_coro::frame_allocator_tag)
        specially, returning an awaiter that yields the stored value.
        All other awaitables are delegated to @ref transform_awaitable.

        @pre @ref set_environment has been called with a non-null
        pointer, if `T` is one of the tag types.

        @param t The awaited expression.

        @return An awaiter for the expression.
    */
    template<typename T>
    auto await_transform(T&& t)
    {
        using Tag = std::decay_t<T>;

        if constexpr (std::is_same_v<Tag, this_coro::environment_tag>)
        {
            BOOST_CAPY_ASSERT(env_);
            struct awaiter
            {
                await_context const* env_;
                bool await_ready() const noexcept { return true; }
                void await_suspend(std::coroutine_handle<>) const noexcept { }
                await_context const* await_resume() const noexcept { return env_; }
            };
            return awaiter{env_};
        }
        else if constexpr (std::is_same_v<Tag, this_coro::executor_tag>)
        {
            BOOST_CAPY_ASSERT(env_);
            struct awaiter
            {
                executor_ref executor_;
                bool await_ready() const noexcept { return true; }
                void await_suspend(std::coroutine_handle<>) const noexcept { } // LCOV_EXCL_LINE await_ready() always true, never suspends
                executor_ref await_resume() const noexcept { return executor_; }
            };
            return awaiter{env_->executor};
        }
        else if constexpr (std::is_same_v<Tag, this_coro::stop_token_tag>)
        {
            BOOST_CAPY_ASSERT(env_);
            struct awaiter
            {
                std::stop_token token_;
                bool await_ready() const noexcept { return true; }
                void await_suspend(std::coroutine_handle<>) const noexcept { } // LCOV_EXCL_LINE await_ready() always true, never suspends
                std::stop_token await_resume() const noexcept { return token_; }
            };
            return awaiter{env_->stop_token};
        }
        else if constexpr (std::is_same_v<Tag, this_coro::frame_allocator_tag>)
        {
            BOOST_CAPY_ASSERT(env_);
            struct awaiter
            {
                std::pmr::memory_resource* frame_allocator_;
                bool await_ready() const noexcept { return true; }
                void await_suspend(std::coroutine_handle<>) const noexcept { } // LCOV_EXCL_LINE await_ready() always true, never suspends
                std::pmr::memory_resource* await_resume() const noexcept { return frame_allocator_; }
            };
            return awaiter{env_->frame_allocator};
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
