//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_GET_STOP_TOKEN_HPP
#define BOOST_CAPY_EX_GET_STOP_TOKEN_HPP

#include <boost/capy/detail/config.hpp>

namespace boost {
namespace capy {

/** Tag type for coroutine stop token retrieval.

    This tag is returned by @ref get_stop_token and intercepted by a
    promise type's `await_transform` to yield the coroutine's current
    stop token. The tag itself carries no data; it serves only as a
    sentinel for compile-time dispatch.

    @see get_stop_token
    @see stop_token_support
*/
struct get_stop_token_tag {};

/** Return a tag that yields the current stop token when awaited.

    Use `co_await get_stop_token()` inside a coroutine whose promise
    type supports stop token access (e.g., inherits from
    @ref stop_token_support). The returned stop token reflects whatever
    token was passed to this coroutine when it was awaited.

    @par Example
    @code
    task<void> cancellable_work()
    {
        auto token = co_await get_stop_token();
        for (int i = 0; i < 1000; ++i)
        {
            if (token.stop_requested())
                co_return;  // Exit gracefully on cancellation
            co_await process_chunk(i);
        }
    }
    @endcode

    @par Behavior
    @li If no stop token was propagated, returns a default-constructed
        `capy::stop_token` (where `stop_possible()` returns `false`).
    @li The returned token remains valid for the coroutine's lifetime.
    @li This operation never suspends; `await_ready()` always returns `true`.

    @return A tag that `await_transform` intercepts to return the stop token.

    @see get_stop_token_tag
    @see stop_token_support
*/
inline get_stop_token_tag get_stop_token() noexcept
{
    return {};
}

} // namespace capy
} // namespace boost

#endif
