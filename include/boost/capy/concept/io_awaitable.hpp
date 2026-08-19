//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_IO_AWAITABLE_HPP
#define BOOST_CAPY_CONCEPT_IO_AWAITABLE_HPP

#include <boost/capy/detail/config.hpp>
#include <coroutine>
#include <boost/capy/ex/io_env.hpp>
#include <ranges>

namespace boost {
namespace capy {

/** Requires `await_suspend` to accept a coroutine handle and an `io_env` pointer.

    An awaitable satisfies `IoAwaitable` if its `await_suspend` accepts
    an `io_env`, enabling scheduler affinity, cancellation, and allocator
    propagation. This extended signature distinguishes I/O awaitables
    from standard C++ awaitables that only take a coroutine handle.

    `IoAwaitable` constrains only this one member function,
    `await_suspend(std::coroutine_handle<>, io_env const*)`. It is the
    single customization point that receives the `io_env`. It is
    therefore the only member that needs the executor, stop token, and
    frame allocator used to start, schedule, and cancel the operation.
    `await_ready` and `await_resume` operate on state local to the
    awaitable and take no `io_env` parameter, so this concept does not
    check them.

    @tparam A The awaitable type.

    @par Syntactic Requirements

    @li `a.await_suspend(h, env)` must be a valid expression where:
        - `h` is a `std::coroutine_handle<>` (coroutine handle).
        - `env` is an `io_env const*`.

    @par Semantic Requirements

    When `await_suspend` is called:

    @li The awaitable uses `env->executor` to schedule
        resumption of the coroutine when the operation completes.
    @li The awaitable should monitor `env->stop_token` and
        complete early with a cancellation error if stop is
        requested.
    @li The awaitable may use `env->frame_allocator` for internal
        allocations.
    @li The awaitable must propagate `env->frame_allocator` faithfully
        to any child coroutines it creates.
    @li The awaitable may return `std::noop_coroutine()` to
        indicate the operation was started asynchronously.

    @par Lifetime

    The `io_env` passed to `await_suspend` remains valid for the
    lifetime of the awaitable's async operation. @ref run,
    @ref run_async and the other functions that start a task
    guarantee this.
    Awaitables that need to retain access to the environment should
    store it as `io_env const*`, never as a copy. Copying is
    unnecessary and wasteful because the referent is guaranteed to
    outlive the operation.

    @par Conforming Signatures

    Only the `await_suspend` overload shown below is checked by
    `IoAwaitable`. `await_ready` and `await_resume` are shown for
    context. The C++ awaitable protocol (`co_await`) requires the
    compiler to find them on the awaiter type. This concept does not
    require them.

    @code
    struct A
    {
        bool await_ready() const noexcept;

        auto await_suspend(
            std::coroutine_handle<> h,
            io_env const* env );

        T await_resume();
    };
    @endcode

    @par Example

    @code
    struct my_io_op
    {
        io_env const* env_ = nullptr;
        continuation cont_;

        auto await_suspend(
            std::coroutine_handle<> h,
            io_env const* env )
        {
            env_ = env;
            cont_ = continuation{h};
            // Pass members by value; capturing this
            // risks use-after-free in async callbacks.
            // When the async operation completes, resume
            // via executor.post(cont_) or executor.dispatch(cont_)
            // rather than calling h.resume() directly.
            start_async(
                env_->stop_token,
                env_->executor,
                cont_ );
            return std::noop_coroutine();
        }

        bool await_ready() const noexcept { return false; }
        void await_resume() {}
    };
    @endcode

    @see IoRunnable
*/
template<typename A>
concept IoAwaitable =
    requires(
        A a,
        std::coroutine_handle<> h,
        io_env const* env)
    {
        a.await_suspend(h, env);
    };

/** Names what `co_await a` yields for awaitable type A.

    Given an awaitable A, yields the type returned by A::await_resume().

    @tparam A The awaitable type.
*/
template<typename A>
using awaitable_result_t = decltype(std::declval<std::decay_t<A>&>().await_resume());

/** Requires a sized input range whose value type satisfies `IoAwaitable`.

    A range satisfies `IoAwaitableRange` if it is a sized input range
    whose value type satisfies @ref IoAwaitable.

    @tparam R The range type.
*/
template<typename R>
concept IoAwaitableRange =
    std::ranges::input_range<R> &&
    std::ranges::sized_range<R> &&
    IoAwaitable<std::ranges::range_value_t<R>>;

} // namespace capy
} // namespace boost

#endif
