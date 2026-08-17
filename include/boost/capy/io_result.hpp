//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_RESULT_HPP
#define BOOST_CAPY_IO_RESULT_HPP

#include <boost/capy/detail/config.hpp>
#include <system_error>

#include <tuple>
#include <type_traits>

namespace boost {
namespace capy {

/** Result type for asynchronous I/O operations.

    This alias provides a unified result type for async operations,
    always containing a `std::error_code` plus optional additional
    values. Because it is a `std::tuple`, results interoperate with
    the entire standard tuple API: structured bindings, `std::tie`,
    `std::apply`, `std::get`, `std::tuple_cat`, comparisons, and
    tuple assignment.

    @par Example
    @code
    auto [ec, n] = co_await s.read_some(buf);
    if (ec) { ... }
    @endcode

    `std::tie` rebinds into existing variables without introducing
    new bindings:
    @code
    std::error_code ec;
    std::size_t n = 0;
    std::tie(ec, n) = co_await s.read_some(buf);
    @endcode

    @note Whether the payload is meaningful when the error code is
        set is defined by the operation that produced the result.
        Many I/O operations report a meaningful partial result
        alongside the error (for example, the number of bytes
        transferred before the condition, as with EOF); others
        leave it unspecified.

    @tparam Ts Ordered payload types following the leading
        `std::error_code`.
*/
template<class... Ts>
using io_result = std::tuple<std::error_code, Ts...>;

namespace detail {

// Outcomes are structural, not nominal: any std::tuple whose first
// element is error_code is an io_result, regardless of how the
// producing operation spelled it.
template<class T>
struct is_io_result : std::false_type {};

template<class... Ts>
struct is_io_result<std::tuple<std::error_code, Ts...>>
    : std::true_type {};

template<class T>
inline constexpr bool is_io_result_v = is_io_result<T>::value;

} // namespace detail

} // namespace capy
} // namespace boost

#endif // BOOST_CAPY_IO_RESULT_HPP
