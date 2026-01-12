//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_AFFINE_AWAITABLE_HPP
#define BOOST_CAPY_CONCEPT_AFFINE_AWAITABLE_HPP

#include <boost/capy/concept/dispatcher.hpp>

#include <coroutine>

namespace boost {
namespace capy {

/** Concept for affine awaitable types.

    An awaitable is affine if it participates in the affine awaitable protocol
    by accepting a dispatcher in its `await_suspend` method. This enables
    zero-overhead scheduler affinity without requiring the full sender/receiver
    protocol.

    @tparam A The awaitable type.
    @tparam D The dispatcher type.
    @tparam P The promise type (defaults to void).

    @par Requirements
    @li `D` must satisfy `dispatcher<D, P>`
    @li `A` must provide `await_suspend(std::coroutine_handle<P> h, D const& d)`
    @li The awaitable must use the dispatcher `d` to resume the caller,
        e.g. `return d(h);`
    @li The dispatcher returns a coroutine handle that `await_suspend` may
        return for symmetric transfer

    @par Example
    @code
    struct my_async_op
    {
        template<typename Dispatcher>
        auto await_suspend(coro h, Dispatcher const& d)
        {
            start_async([h, &d] {
                d(h);  // Schedule resumption through dispatcher
            });
            return std::noop_coroutine();  // Or return d(h) for symmetric transfer
        }
        // ... await_ready, await_resume ...
    };
    @endcode
*/
template<typename A, typename D, typename P = void>
concept affine_awaitable =
    dispatcher<D, P> &&
    requires(A a, std::coroutine_handle<P> h, D const& d) {
        a.await_suspend(h, d);
    };

} // capy
} // boost

#endif
