//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DECOMPOSES_TO_HPP
#define BOOST_CAPY_DECOMPOSES_TO_HPP

#include <boost/capy/detail/config.hpp>

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

struct any_type
{
    template <typename T>
    constexpr operator T() const noexcept;
};

template <typename T, std::size_t N>
concept is_tuple_n = requires {
    std::tuple_size<std::remove_cvref_t<T>>::value;
} && std::tuple_size<std::remove_cvref_t<T>>::value == N;

// clang-format off
template <typename T>
concept is_decomposable_1 =
    (std::is_aggregate_v<std::remove_cvref_t<T>> &&
        requires { std::remove_cvref_t<T>{ any_type{} }; } &&
        !requires { std::remove_cvref_t<T>{ any_type{}, any_type{} }; }
    ) || is_tuple_n<T, 1>;

template <typename T>
concept is_decomposable_2 = 
    (std::is_aggregate_v<std::remove_cvref_t<T>> &&
        requires { std::remove_cvref_t<T>{ any_type{}, any_type{} }; } &&
        !requires { std::remove_cvref_t<T>{ any_type{}, any_type{}, any_type{} }; }
    ) || is_tuple_n<T, 2>;

template <typename T>
concept is_decomposable_3 = 
    (std::is_aggregate_v<std::remove_cvref_t<T>> &&
        requires { std::remove_cvref_t<T>{ any_type{}, any_type{}, any_type{} }; } &&
        !requires { std::remove_cvref_t<T>{ any_type{}, any_type{}, any_type{}, any_type{} }; }
    ) || is_tuple_n<T, 3>;

template <typename T>
concept is_decomposable_4 = 
    (std::is_aggregate_v<std::remove_cvref_t<T>> &&
        requires { std::remove_cvref_t<T>{ any_type{}, any_type{}, any_type{}, any_type{} }; } &&
        !requires { std::remove_cvref_t<T>{ any_type{}, any_type{}, any_type{}, any_type{}, any_type{} }; }
    ) || is_tuple_n<T, 4>;

// clang-format on

template <is_decomposable_1 T>
auto decomposed_types(T&& t)
{
    auto [v0] = t;
    return std::make_tuple(v0);
}

template <is_decomposable_2 T>
auto decomposed_types(T&& t)
{
    auto [v0, v1] = t;
    return std::make_tuple(v0, v1);
}

template <is_decomposable_3 T>
auto decomposed_types(T&& t)
{
    auto [v0, v1, v2] = t;
    return std::make_tuple(v0, v1, v2);
}

template <is_decomposable_4 T>
auto decomposed_types(T&& t)
{
    auto [v0, v1, v2, v3] = t;
    return std::make_tuple(v0, v1, v2, v3);
}

template <class T>
std::tuple<> decomposed_types(T&&)
{
    return {};
}

template<typename T>
auto get_awaiter(T&& t)
{
    if constexpr (requires { std::forward<T>(t).operator co_await(); })
    {
        return std::forward<T>(t).operator co_await();
    }
    else if constexpr (requires { operator co_await(std::forward<T>(t)); })
    {
        return operator co_await(std::forward<T>(t));
    }
    else
    {
        return std::forward<T>(t);
    }
}

template<typename A>
using awaitable_return_t = decltype(
    get_awaiter(std::declval<A>()).await_resume()
);

} // namespace detail

/** Concept for types that decompose to a specific typelist.

    A type satisfies `decomposes_to` if it can be decomposed via
    structured bindings into the specified types. This includes
    aggregates with matching member types and tuple-like types
    with matching element types.

    @tparam T The type to decompose.
    @tparam Types The expected element types after decomposition.

    @par Example
    @code
    struct result { int a; double b; };

    static_assert(decomposes_to<result, int, double>);
    static_assert(decomposes_to<std::tuple<int, double>, int, double>);
    @endcode
*/
template <typename T, typename... Types>
concept decomposes_to = requires(T&& t) {
    { detail::decomposed_types(std::forward<T>(t)) } -> std::same_as<std::tuple<Types...>>;
};

/** Concept for awaitables whose return type decomposes to a specific typelist.

    A type satisfies `awaitable_decomposes_to` if it is an awaitable
    (has `await_resume`) and its return type decomposes to the
    specified typelist.

    @tparam A The awaitable type.
    @tparam Types The expected element types after decomposition.

    @par Requirements
    @li `A` must be an awaitable (directly or via `operator co_await`)
    @li The return type of `await_resume()` must decompose to `Types...`

    @par Example
    @code
    // Constrain a function to accept only awaitables that return
    // a decomposable result of (error_code, size_t)
    template<typename A>
        requires awaitable_decomposes_to<A, system::error_code, std::size_t>
    task<void> process(A&& op)
    {
        auto [ec, n] = co_await std::forward<A>(op);
        if (ec)
            co_return;
        // process n bytes...
    }
    @endcode
*/
template<typename A, typename... Types>
concept awaitable_decomposes_to = requires {
    typename detail::awaitable_return_t<A>;
} && decomposes_to<detail::awaitable_return_t<A>, Types...>;

} // namespace capy
} // namespace boost

#endif
