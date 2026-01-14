//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_STOPPABLE_AWAITABLE_HPP
#define BOOST_CAPY_CONCEPT_STOPPABLE_AWAITABLE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/affine_awaitable.hpp>

#include <coroutine>
#if BOOST_CAPY_HAS_STOP_TOKEN
#include <stop_token>
#endif

namespace boost {
namespace capy {

#if BOOST_CAPY_HAS_STOP_TOKEN

/** Concept for stoppable awaitable types.

    An awaitable is stoppable if it participates in the stoppable awaitable
    protocol by accepting both a dispatcher and a stop_token in its
    `await_suspend` method. This extends the affine awaitable protocol to
    enable automatic stop token propagation through coroutine chains.

    @tparam A The awaitable type.
    @tparam D The dispatcher type.
    @tparam P The promise type (defaults to void).

    @par Requirements
    @li `A` must satisfy `affine_awaitable<A, D, P>`
    @li `A` must provide `await_suspend(std::coroutine_handle<P> h, D const& d,
        std::stop_token token)`
    @li The awaitable should use the stop_token to support cancellation
    @li The awaitable must use the dispatcher `d` to resume the caller

    @par Example
    @code
    struct my_stoppable_op
    {
        template<typename Dispatcher>
        auto await_suspend(any_coro h, Dispatcher const& d, std::stop_token token)
        {
            start_async([h, &d, token] {
                if (token.stop_requested()) {
                    // Handle cancellation
                }
                d(h);  // Schedule resumption through dispatcher
            });
            return std::noop_coroutine();
        }
        // ... await_ready, await_resume ...
    };
    @endcode

    @see affine_awaitable
    @see dispatcher
*/
template<typename A, typename D, typename P = void>
concept stoppable_awaitable =
    affine_awaitable<A, D, P> &&
    requires(A a, std::coroutine_handle<P> h, D const& d, std::stop_token token) {
        a.await_suspend(h, d, token);
    };

#endif // BOOST_CAPY_HAS_STOP_TOKEN

} // capy
} // boost

#endif
