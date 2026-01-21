//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
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
#include <stop_token>

namespace boost {
namespace capy {

class executor_ref;

/** Concept for I/O awaitable types.

    An awaitable is an I/O awaitable if it participates in the I/O awaitable
    protocol by accepting an executor and a stop_token in its `await_suspend`
    method. This enables zero-overhead scheduler affinity and cancellation
    support.

    @tparam A The awaitable type.
    @tparam Ex The executor type (unconstrained, must have post/dispatch).
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

} // capy
} // boost

#endif
