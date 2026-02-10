//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_IO_AWAITABLE_HPP
#define BOOST_CAPY_CONCEPT_IO_AWAITABLE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/io_env.hpp>

namespace boost {
namespace capy {

/** Concept for awaitables that participate in the I/O protocol.

    An awaitable satisfies `IoAwaitable` if its `await_suspend` accepts
    an `io_env`, enabling scheduler affinity, cancellation, and allocator
    propagation. This extended signature distinguishes I/O awaitables
    from standard C++ awaitables that only take a coroutine handle.

    @tparam A The awaitable type.

    @par Syntactic Requirements

    @li `a.await_suspend(h, env)` must be a valid expression where:
        - `h` is a `coro` (coroutine handle)
        - `env` is an `io_env const&`

    @par Semantic Requirements

    The `await_suspend` operation initiates the async operation:

    @li The awaitable stores the executor from `env` and uses it to
        schedule resumption of the coroutine when the operation completes
    @li The awaitable should monitor `env.stop_token` and complete
        early with a cancellation error if stop is requested
    @li The awaitable may use `env.allocator` for internal allocations
    @li The awaitable may return `std::noop_coroutine()` to indicate
        the operation was started asynchronously

    @par Lifetime

    The `io_env` passed to `await_suspend` is guaranteed by launch
    functions (`run_async`, `run`) to remain valid for the lifetime
    of the awaitable's async operation. Awaitables that need to retain
    access to the environment should store it as `io_env const*`,
    never as a copy. Copying is unnecessary and wasteful because the
    referent is guaranteed to outlive the operation.

    @par Conforming Signatures

    @code
    struct A
    {
        bool await_ready() const noexcept;

        auto await_suspend(
            coro h,
            io_env const& env );

        T await_resume();
    };
    @endcode

    @par Example

    @code
    struct my_io_op
    {
        io_env const* env_ = nullptr;
        coro cont_;

        auto await_suspend(
            coro h,
            io_env const& env )
        {
            env_ = &env;  // Store pointer, never copy
            cont_ = h;
            start_async( [this] {
                if( env_->stop_token.stop_requested() )
                {
                    // complete with cancellation error
                }
                env_->executor.dispatch( cont_ );
            } );
            return std::noop_coroutine();
        }

        bool await_ready() const noexcept { return false; }
        void await_resume() {}
    };
    @endcode

    @see IoAwaitableTask
*/
template<typename A>
concept IoAwaitable =
    requires(
        A a,
        coro h,
        io_env const& env)
    {
        a.await_suspend(h, env);
    };

namespace detail {

/** Extract the result type from any awaitable via await_resume().
*/
template<typename A>
using awaitable_result_t = decltype(std::declval<std::decay_t<A>&>().await_resume());

} // namespace detail

} // namespace capy
} // namespace boost

#endif
