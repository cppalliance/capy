//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TIMEOUT_HPP
#define BOOST_CAPY_TIMEOUT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/delay.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_any.hpp>

#include <chrono>
#include <system_error>
#include <type_traits>

namespace boost {
namespace capy {

namespace detail {

template<typename T>
struct is_io_result : std::false_type {};

template<typename... Args>
struct is_io_result<io_result<Args...>> : std::true_type {};

template<typename T>
inline constexpr bool is_io_result_v = is_io_result<T>::value;

} // detail

/** Race an awaitable against a deadline.

    Starts the awaitable and a timer concurrently. If the
    awaitable completes first, its result is returned. If the
    timer fires first, stop is requested for the awaitable and
    a timeout error is produced.

    @par Return Type

    The return type matches the inner awaitable's result type:

    @li For `io_result<...>` types: returns `io_result` with
        `ec == error::timeout` and default-initialized values
    @li For non-void types: throws `std::system_error(error::timeout)`
    @li For void: throws `std::system_error(error::timeout)`

    @par Precision

    The timeout fires at or after the specified duration.

    @par Cancellation

    If the parent's stop token is activated, the inner awaitable
    is cancelled normally (not a timeout). The result reflects
    the inner awaitable's cancellation behavior.

    @par Example
    @code
    auto [ec, n] = co_await timeout(sock.read_some(buf), 50ms);
    if (ec == cond::timeout) {
        // handle timeout
    }
    @endcode

    @tparam A An IoAwaitable whose result type determines
        how timeouts are reported.

    @param a The awaitable to race against the deadline.
    @param dur The maximum duration to wait.

    @return `task<awaitable_result_t<A>>`.

    @throws std::system_error with `error::timeout` if the timer
        fires first and the result type is not `io_result`.
        Exceptions thrown by the inner awaitable propagate
        unchanged.

    @see delay, when_any, cond::timeout
*/
template<IoAwaitable A, typename Rep, typename Period>
auto timeout(A a, std::chrono::duration<Rep, Period> dur)
    -> task<awaitable_result_t<A>>
{
    using T = awaitable_result_t<A>;

    auto result = co_await when_any(
        std::move(a), delay(dur));

    if(result.index() == 0)
    {
        // Task completed first
        if constexpr (std::is_void_v<T>)
            co_return;
        else
            co_return std::get<0>(std::move(result));
    }
    else
    {
        // Timer won
        if constexpr (detail::is_io_result_v<T>)
        {
            T timeout_result{};
            timeout_result.ec = make_error_code(error::timeout);
            co_return timeout_result;
        }
        else if constexpr (std::is_void_v<T>)
        {
            throw std::system_error(
                make_error_code(error::timeout));
        }
        else
        {
            throw std::system_error(
                make_error_code(error::timeout));
        }
    }
}

} // capy
} // boost

#endif
