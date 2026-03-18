//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_IO_RESULT_COMBINATORS_HPP
#define BOOST_CAPY_DETAIL_IO_RESULT_COMBINATORS_HPP

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/io_result.hpp>

#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

template<typename T>
struct is_io_result : std::false_type {};

template<typename... Args>
struct is_io_result<io_result<Args...>> : std::true_type {};

template<typename T>
inline constexpr bool is_io_result_v = is_io_result<T>::value;

/// True when every awaitable in the pack returns an io_result.
template<typename... As>
concept all_io_result_awaitables =
    (is_io_result_v<awaitable_result_t<As>> && ...);

/// True when the io_result-aware when_all overload should be used.
template<typename... As>
concept when_all_io_eligible =
    (sizeof...(As) > 0)
    && all_io_result_awaitables<As...>;

/// True when the io_result-aware when_any overload should be used.
template<typename... As>
concept when_any_io_eligible =
    (sizeof...(As) > 0)
    && all_io_result_awaitables<As...>;

/// Map an io_result specialization to its contributed payload type.
///
///   io_result<T>       -> T            (unwrap single)
///   io_result<Ts...>   -> tuple<Ts...> (zero, two, or more)
template<typename IoResult>
struct io_result_payload;

template<typename T>
struct io_result_payload<io_result<T>>
{
    using type = T;
};

template<typename... Ts>
struct io_result_payload<io_result<Ts...>>
{
    using type = std::tuple<Ts...>;
};

template<typename IoResult>
using io_result_payload_t =
    typename io_result_payload<IoResult>::type;

/// Extract the payload value(s) from an io_result,
/// matching the type produced by io_result_payload_t.
template<typename T>
T
extract_io_payload(io_result<T>&& r)
{
    return std::get<0>(std::move(r.values));
}

template<typename... Ts>
std::tuple<Ts...>
extract_io_payload(io_result<Ts...>&& r)
{
    return std::move(r.values);
}

/// Reconstruct a success io_result from a payload extracted by when_any.
template<typename IoResult>
struct io_result_from_payload;

template<typename T>
struct io_result_from_payload<io_result<T>>
{
    static io_result<T> apply(T t)
    {
        return io_result<T>{{}, std::move(t)};
    }
};

template<typename... Ts>
struct io_result_from_payload<io_result<Ts...>>
{
    static io_result<Ts...> apply(std::tuple<Ts...> t)
    {
        return std::apply([](auto&&... args) {
            return io_result<Ts...>{{}, std::move(args)...};
        }, std::move(t));
    }
};

/// Build the outer io_result for when_all from a tuple of child io_results.
template<typename ResultType, typename Tuple, std::size_t... Is>
ResultType
build_when_all_io_result_impl(Tuple&& results, std::index_sequence<Is...>)
{
    std::error_code ec;
    (void)((std::get<Is>(results).ec && !ec
        ? (ec = std::get<Is>(results).ec, true)
        : false) || ...);

    return ResultType{ec, extract_io_payload(
        std::move(std::get<Is>(results)))...};
}

template<typename ResultType, typename... IoResults>
ResultType
build_when_all_io_result(std::tuple<IoResults...>&& results)
{
    return build_when_all_io_result_impl<ResultType>(
        std::move(results),
        std::index_sequence_for<IoResults...>{});
}

} // namespace detail
} // namespace capy
} // namespace boost

#endif
