//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TYPE_TRAITS_HPP
#define BOOST_CAPY_TYPE_TRAITS_HPP

#include <boost/capy/detail/config.hpp>

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

namespace detail {

template<typename T, std::size_t... Is>
auto decomposed_types_impl(std::index_sequence<Is...>)
    -> std::tuple<std::tuple_element_t<Is, std::remove_cvref_t<T>>...>;

template<typename T>
using decomposed_types_t = decltype(
    decomposed_types_impl<T>(
        std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<T>>>{}
    )
);

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

    A type satisfies `decomposes_to` if it supports structured bindings
    via the tuple protocol and its element types match the specified
    typelist exactly.

    @tparam T The type to check.
    @tparam Types The expected element types after decomposition.

    @par Requirements
    @li `T` must satisfy the tuple protocol (`std::tuple_size`,
        `std::tuple_element`)
    @li The number of elements must equal `sizeof...(Types)`
    @li Each element type must match the corresponding type in `Types`

    @par Example
    @code
    static_assert(decomposes_to<std::pair<int, double>, int, double>);
    static_assert(decomposes_to<std::tuple<int, float, char>, int, float, char>);
    static_assert(decomposes_to<std::array<int, 3>, int, int, int>);

    // Constrain a function template
    template<typename T>
        requires decomposes_to<T, system::error_code, std::size_t>
    void process_result(T&& result)
    {
        auto [ec, n] = std::forward<T>(result);
        // ...
    }
    @endcode

    @note Plain aggregates without the tuple protocol are not supported.
        Use `std::pair`, `std::tuple`, `std::array`, or add the tuple
        protocol to your type.
*/
template<typename T, typename... Types>
concept decomposes_to = std::same_as<
    detail::decomposed_types_t<T>,
    std::tuple<Types...>
>;

/** Concept for awaitables whose return type decomposes to a specific typelist.

    A type satisfies `awaitable_decomposes_to` if it is an awaitable
    (has `await_resume`) and its return type satisfies @ref decomposes_to
    with the specified typelist.

    @tparam A The awaitable type.
    @tparam Types The expected element types after decomposition.

    @par Requirements
    @li `A` must be an awaitable (directly or via `operator co_await`)
    @li The return type of `await_resume()` must satisfy @ref decomposes_to
        with `Types...`

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

    @see decomposes_to
*/
template<typename A, typename... Types>
concept awaitable_decomposes_to = requires {
    typename detail::awaitable_return_t<A>;
} && decomposes_to<detail::awaitable_return_t<A>, Types...>;

} // namespace capy
} // namespace boost

#endif
