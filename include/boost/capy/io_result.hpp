//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_RESULT_HPP
#define BOOST_CAPY_IO_RESULT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/system/error_code.hpp>

namespace boost {
namespace capy {

/** Result type for asynchronous I/O operations.

    This template provides a unified result type for async operations,
    always containing a `system::error_code` plus optional additional
    values. It supports structured bindings.

    @tparam Args Additional value types beyond the error code.

    @par Usage
    @code
    // Error code path (default)
    auto [ec, n] = co_await s.read_some(buf);
    if (ec) { ... }

    // Exception path (opt-in)
    auto n = (co_await s.read_some(buf)).value();
    @endcode
*/
template<class... Args>
struct io_result
{
    static_assert("io_result only supports up to 3 template arguments");
};

/** Result type for void operations.

    Used by operations like `connect()` that don't return a value
    beyond success/failure.

    @par Example
    @code
    auto [ec] = co_await s.connect(ep);
    if (ec) { ... }

    // Or with exceptions:
    (co_await s.connect(ep)).value();
    @endcode
*/
template<>
struct [[nodiscard]] io_result<>
{
    /** The error code from the operation. */
    system::error_code ec;
};

/** Result type for byte transfer operations.

    Used by operations like `read_some()` and `write_some()` that
    return the number of bytes transferred.

    @par Example
    @code
    auto [ec, n] = co_await s.read_some(buf);
    if (ec) { ... }

    // Or with exceptions:
    auto n = (co_await s.read_some(buf)).value();
    @endcode
*/
template<typename T1>
struct [[nodiscard]] io_result<T1>
{
    system::error_code ec;
    T1 t1{};
};

template<typename T1, typename T2>
struct [[nodiscard]] io_result<T1, T2>
{
    system::error_code ec;
    T1 t1{};
    T2 t2{};
};

template<typename T1, typename T2, typename T3>
struct [[nodiscard]] io_result<T1, T2, T3>
{
    system::error_code ec;
    T1 t1{};
    T2 t2{};
    T3 t3{};
};

} // namespace capy
} // namespace boost

#endif
