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

#include <cstddef>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

/** Result type for asynchronous I/O operations.

    This template provides a unified result type for async operations,
    always containing a `std::error_code` plus optional additional
    values. It supports structured bindings.

    @par Example
    @code
    auto [ec, n] = co_await s.read_some(buf);
    if (ec) { ... }
    @endcode

    @note Only 0, 1, 2, and 3 payload specializations are
        provided. Payload members are only meaningful when
        `ec` does not indicate an error.

    @tparam Args Ordered payload types following the leading
        `std::error_code`.
*/
template<class... Args>
struct io_result
{
    static_assert("io_result only supports up to 3 template arguments");
};

/** Result type for void operations.

    Used by operations like `connect()` that don't return a value
    beyond success/failure. This specialization is not an aggregate
    to enable implicit conversion from `error_code`.

    @par Example
    @code
    auto [ec] = co_await s.connect(ep);
    if (ec) { ... }
    @endcode
*/
template<>
struct [[nodiscard]] io_result<>
{
    /** The error code from the operation. */
    std::error_code ec;

#if BOOST_CAPY_WORKAROUND(_MSC_VER, >= 1)
    // Tuple protocol (unconditional - io_result<> is not an aggregate)
    template<std::size_t I>
    auto& get() & noexcept
    {
        static_assert(I == 0, "index out of range");
        return ec;
    }

    template<std::size_t I>
    auto const& get() const& noexcept
    {
        static_assert(I == 0, "index out of range");
        return ec;
    }

    template<std::size_t I>
    auto&& get() && noexcept
    {
        static_assert(I == 0, "index out of range");
        return std::move(ec);
    }
#endif
};

/** Result type for byte transfer operations.

    Used by operations like `read_some()` and `write_some()` that
    return the number of bytes transferred.

    @par Example
    @code
    auto [ec, n] = co_await s.read_some(buf);
    if (ec) { ... }
    @endcode
*/
template<typename T1>
struct [[nodiscard]] io_result<T1>
{
    /// The error code from the operation.
    std::error_code ec;

    /// The first payload value. Unspecified when `ec` is set.
    T1 t1{};

#if BOOST_CAPY_WORKAROUND(_MSC_VER, >= 1)
    template<std::size_t I>
    auto& get() & noexcept
    {
        static_assert(I < 2, "index out of range");
        if constexpr (I == 0) return ec;
        else return t1;
    }

    template<std::size_t I>
    auto const& get() const& noexcept
    {
        static_assert(I < 2, "index out of range");
        if constexpr (I == 0) return ec;
        else return t1;
    }

    template<std::size_t I>
    auto&& get() && noexcept
    {
        static_assert(I < 2, "index out of range");
        if constexpr (I == 0) return std::move(ec);
        else return std::move(t1);
    }
#endif
};

/** Result type for operations returning two values.

    Stores a leading `std::error_code` followed by two
    operation-specific payload values. Inspect `t1` and `t2`
    only when `ec` does not indicate an error.

    @par Example
    @code
    auto [ec, t1, t2] = co_await some_op();
    if (ec) { ... }
    @endcode

    @tparam T1 First payload type following the error code.
    @tparam T2 Second payload type following T1.

    @see io_result
*/
template<typename T1, typename T2>
struct [[nodiscard]] io_result<T1, T2>
{
    /// The error code from the operation.
    std::error_code ec;

    /// The first payload value. Unspecified when `ec` is set.
    T1 t1{};

    /// The second payload value. Unspecified when `ec` is set.
    T2 t2{};

#if BOOST_CAPY_WORKAROUND(_MSC_VER, >= 1)
    template<std::size_t I>
    auto& get() & noexcept
    {
        static_assert(I < 3, "index out of range");
        if constexpr (I == 0) return ec;
        else if constexpr (I == 1) return t1;
        else return t2;
    }

    template<std::size_t I>
    auto const& get() const& noexcept
    {
        static_assert(I < 3, "index out of range");
        if constexpr (I == 0) return ec;
        else if constexpr (I == 1) return t1;
        else return t2;
    }

    template<std::size_t I>
    auto&& get() && noexcept
    {
        static_assert(I < 3, "index out of range");
        if constexpr (I == 0) return std::move(ec);
        else if constexpr (I == 1) return std::move(t1);
        else return std::move(t2);
    }
#endif
};

/** Result type for operations returning three values.

    Stores a leading `std::error_code` followed by three
    operation-specific payload values. Inspect `t1`, `t2`,
    and `t3` only when `ec` does not indicate an error.

    @par Example
    @code
    auto [ec, t1, t2, t3] = co_await some_op();
    if (ec) { ... }
    @endcode

    @tparam T1 First payload type following the error code.
    @tparam T2 Second payload type following T1.
    @tparam T3 Third payload type following T2.

    @see io_result
*/
template<typename T1, typename T2, typename T3>
struct [[nodiscard]] io_result<T1, T2, T3>
{
    /// The error code from the operation.
    std::error_code ec;

    /// The first payload value. Unspecified when `ec` is set.
    T1 t1{};

    /// The second payload value. Unspecified when `ec` is set.
    T2 t2{};

    /// The third payload value. Unspecified when `ec` is set.
    T3 t3{};

#if BOOST_CAPY_WORKAROUND(_MSC_VER, >= 1)
    template<std::size_t I>
    auto& get() & noexcept
    {
        static_assert(I < 4, "index out of range");
        if constexpr (I == 0) return ec;
        else if constexpr (I == 1) return t1;
        else if constexpr (I == 2) return t2;
        else return t3;
    }

    template<std::size_t I>
    auto const& get() const& noexcept
    {
        static_assert(I < 4, "index out of range");
        if constexpr (I == 0) return ec;
        else if constexpr (I == 1) return t1;
        else if constexpr (I == 2) return t2;
        else return t3;
    }

    template<std::size_t I>
    auto&& get() && noexcept
    {
        static_assert(I < 4, "index out of range");
        if constexpr (I == 0) return std::move(ec);
        else if constexpr (I == 1) return std::move(t1);
        else if constexpr (I == 2) return std::move(t2);
        else return std::move(t3);
    }
#endif
};

#if BOOST_CAPY_WORKAROUND(_MSC_VER, >= 1)

// Free-standing get() overloads for ADL (MSVC aggregate workaround).
/// @cond

template<std::size_t I>
auto& get(io_result<>& r) noexcept
{
    return r.template get<I>();
}

template<std::size_t I>
auto const& get(io_result<> const& r) noexcept
{
    return r.template get<I>();
}

template<std::size_t I>
auto&& get(io_result<>&& r) noexcept
{
    return std::move(r).template get<I>();
}

template<std::size_t I, typename T1>
auto& get(io_result<T1>& r) noexcept
{
    return r.template get<I>();
}

template<std::size_t I, typename T1>
auto const& get(io_result<T1> const& r) noexcept
{
    return r.template get<I>();
}

template<std::size_t I, typename T1>
auto&& get(io_result<T1>&& r) noexcept
{
    return std::move(r).template get<I>();
}

template<std::size_t I, typename T1, typename T2>
auto& get(io_result<T1, T2>& r) noexcept
{
    return r.template get<I>();
}

template<std::size_t I, typename T1, typename T2>
auto const& get(io_result<T1, T2> const& r) noexcept
{
    return r.template get<I>();
}

template<std::size_t I, typename T1, typename T2>
auto&& get(io_result<T1, T2>&& r) noexcept
{
    return std::move(r).template get<I>();
}

template<std::size_t I, typename T1, typename T2, typename T3>
auto& get(io_result<T1, T2, T3>& r) noexcept
{
    return r.template get<I>();
}

template<std::size_t I, typename T1, typename T2, typename T3>
auto const& get(io_result<T1, T2, T3> const& r) noexcept
{
    return r.template get<I>();
}

template<std::size_t I, typename T1, typename T2, typename T3>
auto&& get(io_result<T1, T2, T3>&& r) noexcept
{
    return std::move(r).template get<I>();
}

/// @endcond

#endif // BOOST_CAPY_WORKAROUND(_MSC_VER, >= 1)

} // namespace capy
} // namespace boost

#if BOOST_CAPY_WORKAROUND(_MSC_VER, >= 1)

// Tuple protocol for structured bindings (MSVC workaround)
// MSVC has a bug with aggregate decomposition in coroutines, so we use
// tuple protocol instead which forces the compiler to use get<>() functions.

namespace std {

template<>
struct tuple_size<boost::capy::io_result<>>
    : std::integral_constant<std::size_t, 1> {};

template<>
struct tuple_element<0, boost::capy::io_result<>>
{
    using type = ::std::error_code;
};

template<typename T1>
struct tuple_size<boost::capy::io_result<T1>>
    : std::integral_constant<std::size_t, 2> {};

template<typename T1, typename T2>
struct tuple_size<boost::capy::io_result<T1, T2>>
    : std::integral_constant<std::size_t, 3> {};

template<typename T1, typename T2, typename T3>
struct tuple_size<boost::capy::io_result<T1, T2, T3>>
    : std::integral_constant<std::size_t, 4> {};

// tuple_element specializations for io_result<T1>

template<>
struct tuple_element<0, boost::capy::io_result<std::size_t>>
{
    using type = ::std::error_code;
};

template<>
struct tuple_element<1, boost::capy::io_result<std::size_t>>
{
    using type = std::size_t;
};

template<typename T1>
struct tuple_element<0, boost::capy::io_result<T1>>
{
    using type = ::std::error_code;
};

template<typename T1>
struct tuple_element<1, boost::capy::io_result<T1>>
{
    using type = T1;
};

// tuple_element specializations for io_result<T1, T2>

template<typename T1, typename T2>
struct tuple_element<0, boost::capy::io_result<T1, T2>>
{
    using type = ::std::error_code;
};

template<typename T1, typename T2>
struct tuple_element<1, boost::capy::io_result<T1, T2>>
{
    using type = T1;
};

template<typename T1, typename T2>
struct tuple_element<2, boost::capy::io_result<T1, T2>>
{
    using type = T2;
};

// tuple_element specializations for io_result<T1, T2, T3>

template<typename T1, typename T2, typename T3>
struct tuple_element<0, boost::capy::io_result<T1, T2, T3>>
{
    using type = ::std::error_code;
};

template<typename T1, typename T2, typename T3>
struct tuple_element<1, boost::capy::io_result<T1, T2, T3>>
{
    using type = T1;
};

template<typename T1, typename T2, typename T3>
struct tuple_element<2, boost::capy::io_result<T1, T2, T3>>
{
    using type = T2;
};

template<typename T1, typename T2, typename T3>
struct tuple_element<3, boost::capy::io_result<T1, T2, T3>>
{
    using type = T3;
};

} // namespace std

#endif // BOOST_CAPY_WORKAROUND(_MSC_VER, >= 1)

#endif // BOOST_CAPY_IO_RESULT_HPP
