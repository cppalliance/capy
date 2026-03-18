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
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

/** Result type for asynchronous I/O operations.

    This template provides a unified result type for async operations,
    always containing a `std::error_code` plus optional additional
    values. It supports structured bindings via the tuple protocol.

    @par Example
    @code
    auto [ec, n] = co_await s.read_some(buf);
    if (ec) { ... }
    @endcode

    @note Payload members are only meaningful when
        `ec` does not indicate an error.

    @tparam Ts Ordered payload types following the leading
        `std::error_code`.
*/
template<class... Ts>
struct [[nodiscard]] io_result
{
    /// The error code from the operation.
    std::error_code ec;

    /// The payload values. Unspecified when `ec` is set.
    std::tuple<Ts...> values;

    /// Construct a default io_result.
    io_result() = default;

    /// Construct from an error code and payload values.
    io_result(std::error_code ec_, Ts... ts)
        : ec(ec_)
        , values(std::move(ts)...)
    {
    }

    /// @cond
    template<std::size_t I>
    decltype(auto) get() & noexcept
    {
        static_assert(I < 1 + sizeof...(Ts), "index out of range");
        if constexpr (I == 0) return (ec);
        else return std::get<I - 1>(values);
    }

    template<std::size_t I>
    decltype(auto) get() const& noexcept
    {
        static_assert(I < 1 + sizeof...(Ts), "index out of range");
        if constexpr (I == 0) return (ec);
        else return std::get<I - 1>(values);
    }

    template<std::size_t I>
    decltype(auto) get() && noexcept
    {
        static_assert(I < 1 + sizeof...(Ts), "index out of range");
        if constexpr (I == 0) return std::move(ec);
        else return std::get<I - 1>(std::move(values));
    }
    /// @endcond
};

/// @cond
template<std::size_t I, class... Ts>
decltype(auto) get(io_result<Ts...>& r) noexcept
{
    return r.template get<I>();
}

template<std::size_t I, class... Ts>
decltype(auto) get(io_result<Ts...> const& r) noexcept
{
    return r.template get<I>();
}

template<std::size_t I, class... Ts>
decltype(auto) get(io_result<Ts...>&& r) noexcept
{
    return std::move(r).template get<I>();
}
/// @endcond

} // namespace capy
} // namespace boost

// Tuple protocol for structured bindings
namespace std {

template<class... Ts>
struct tuple_size<boost::capy::io_result<Ts...>>
    : std::integral_constant<std::size_t, 1 + sizeof...(Ts)> {};

template<class... Ts>
struct tuple_element<0, boost::capy::io_result<Ts...>>
{
    using type = std::error_code;
};

template<std::size_t I, class... Ts>
struct tuple_element<I, boost::capy::io_result<Ts...>>
{
    using type = std::tuple_element_t<I - 1, std::tuple<Ts...>>;
};

} // namespace std

#endif // BOOST_CAPY_IO_RESULT_HPP
