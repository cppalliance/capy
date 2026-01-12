//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_DISPATCHER_HPP
#define BOOST_CAPY_CONCEPT_DISPATCHER_HPP

#include <boost/capy/coro.hpp>

#include <concepts>
#include <coroutine>

namespace boost {
namespace capy {

/** Concept for dispatcher types.

    A dispatcher is a callable object that accepts a coroutine handle
    and schedules it for resumption. The dispatcher is responsible for
    ensuring the handle is eventually resumed on the appropriate execution
    context.

    @tparam D The dispatcher type.
    @tparam P The promise type (defaults to void).

    @par Requirements
    @li `d(h)` must be valid where `h` is `std::coroutine_handle<P>` and
        `d` is a const reference to `D`
    @li `d(h)` must return a `coro` (or convertible type)
        to enable symmetric transfer
    @li Calling `d(h)` schedules `h` for resumption (typically by scheduling
        it on a specific execution context) and returns a coroutine handle
        that the caller may use for symmetric transfer
    @li The dispatcher must be const-callable (logical constness), enabling
        thread-safe concurrent dispatch from multiple coroutines

    @note Since `coro` has `operator()` which invokes `resume()`, the handle
    itself is callable and can be dispatched directly.
*/
template<typename D, typename P = void>
concept dispatcher = requires(D const& d, std::coroutine_handle<P> h) {
    { d(h) } -> std::convertible_to<coro>;
};

} // capy
} // boost

#endif
