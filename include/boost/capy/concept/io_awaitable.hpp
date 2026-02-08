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
#include <boost/capy/ex/executor_ref.hpp>

#include <stop_token>

namespace boost {
namespace capy {

/** Concept for awaitables that participate in the I/O protocol.

    An awaitable satisfies `IoAwaitable` if its `await_suspend` accepts
    an executor and stop token, enabling scheduler affinity and cancellation.
    This extended signature distinguishes I/O awaitables from standard
    C++ awaitables that only take a coroutine handle.

    @tparam A The awaitable type.

    @par Syntactic Requirements

    @li `a.await_suspend(h, ex, token)` must be a valid expression where:
        - `h` is a `coro` (coroutine handle)
        - `ex` is an `executor_ref`
        - `token` is a `std::stop_token`

    @par Semantic Requirements

    The `await_suspend` operation initiates the async operation:

    @li The awaitable stores the executor and uses it to schedule
        resumption of the coroutine when the operation completes
    @li The awaitable should monitor the stop token and complete
        early with a cancellation error if stop is requested
    @li The awaitable may return `std::noop_coroutine()` to indicate
        the operation was started asynchronously

    @par Conforming Signatures

    @code
    struct A
    {
        bool await_ready() const noexcept;

        auto await_suspend(
            coro h,
            executor_ref const& ex,
            std::stop_token const& token );

        T await_resume();
    };
    @endcode

    @par Example

    @code
    struct my_io_op
    {
        auto await_suspend(
            coro h,
            executor_ref const& ex,
            std::stop_token const& token )
        {
            start_async( [h, ex, token] {
                if( token.stop_requested() )
                {
                    // complete with cancellation error
                }
                ex.dispatch( h );
            } );
            return std::noop_coroutine();
        }

        bool await_ready() const noexcept { return false; }
        void await_resume() {}
    };
    @endcode

    @see IoAwaitableTask, awaitable_decomposes_to
*/
template<typename A>
concept IoAwaitable =
    requires(
        A a,
        coro h,
        executor_ref ex,
        std::stop_token token)
    {
        a.await_suspend(h, ex, token);
    };

} // namespace capy
} // namespace boost

#endif
