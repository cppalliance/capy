//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_RESULTS_COMBINATORS_HPP
#define BOOST_CAPY_DETAIL_RESULTS_COMBINATORS_HPP

#include <boost/capy/concept/context_awaitable.hpp>
#include <boost/capy/results.hpp>

#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

/// True when every awaitable in the pack returns a `results`.
template<typename... As>
concept all_results_awaitables =
    (is_results_v<awaitable_result_t<As>> && ...);

/// True when the `results`-aware when_all overload should be used.
template<typename... As>
concept when_all_results_eligible =
    (sizeof...(As) > 0)
    && all_results_awaitables<As...>;

/// True when the `results`-aware when_any overload should be used.
template<typename... As>
concept when_any_results_eligible =
    (sizeof...(As) > 0)
    && all_results_awaitables<As...>;

/// Map a `results` specialization to its contributed payload type.
///
///   results<T>       -> T            (unwrap single)
///   results<Ts...>   -> tuple<Ts...> (zero, two, or more)
template<typename Result>
struct results_payload;

template<typename T>
struct results_payload<results<T>>
{
    using type = T;
};

template<typename... Ts>
struct results_payload<results<Ts...>>
{
    using type = std::tuple<Ts...>;
};

template<typename Result>
using results_payload_t =
    typename results_payload<Result>::type;

/// Extract the payload value(s) from a `results`,
/// matching the type produced by results_payload_t.
template<typename T>
T
extract_results_payload(results<T>&& r)
{
    return std::get<1>(std::move(r));
}

template<typename... Ts>
std::tuple<Ts...>
extract_results_payload(results<Ts...>&& r)
{
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return std::tuple<Ts...>(std::get<Is + 1>(std::move(r))...);
    }(std::index_sequence_for<Ts...>{});
}

/// Reconstruct a success `results` from a payload extracted by when_any.
template<typename Result>
struct results_from_payload;

template<typename T>
struct results_from_payload<results<T>>
{
    static results<T> apply(T t)
    {
        return results<T>{std::error_code(), std::move(t)};
    }
};

template<typename... Ts>
struct results_from_payload<results<Ts...>>
{
    static results<Ts...> apply(std::tuple<Ts...> t)
    {
        return std::tuple_cat(
            std::tuple<std::error_code>(), std::move(t));
    }
};

/// Build the outer `results` for when_all from a tuple of child `results`.
template<typename ResultType, typename Tuple, std::size_t... Is>
ResultType
build_when_all_results_impl(Tuple&& results, std::index_sequence<Is...>)
{
    std::error_code ec;
    (void)((std::get<0>(std::get<Is>(results)) && !ec
        ? (ec = std::get<0>(std::get<Is>(results)), true)
        : false) || ...);

    return ResultType{ec, extract_results_payload(
        std::move(std::get<Is>(results)))...};
}

template<typename ResultType, typename... Results>
ResultType
build_when_all_results(std::tuple<Results...>&& results)
{
    return build_when_all_results_impl<ResultType>(
        std::move(results),
        std::index_sequence_for<Results...>{});
}

} // namespace detail
} // namespace capy
} // namespace boost

#endif
