//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_CALL_TRAITS_HPP
#define BOOST_CAPY_DETAIL_CALL_TRAITS_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/core/detail/type_traits.hpp>
#include <boost/mp11/list.hpp>
#include <boost/mp11/algorithm.hpp>

#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

// call_traits

template<class T, class = void>
struct call_traits : std::false_type
{
};

template<class R, class... Args>
struct call_traits<R(*)(Args...)> : std::true_type
{
    using return_type = R;
    using arg_types = mp11::mp_list<Args...>;
};

template<class R, class... Args>
struct call_traits<R(&)(Args...)> : std::true_type
{
    using return_type = R;
    using arg_types = mp11::mp_list<Args...>;
};

template<class C, class R, class... Args>
struct call_traits<R(C::*)(Args...)> : std::true_type
{
    using class_type = C;
    using return_type = R ;
    using arg_types = mp11::mp_list<Args...>;
};

template<class C, class R, class... Args>
struct call_traits<R(C::*)(Args...) const> : std::true_type
{
    using class_type = C;
    using return_type = R ;
    using arg_types = mp11::mp_list<Args...>;
};

template<class F>
struct call_traits<F, typename std::enable_if<
        std::is_member_function_pointer<decltype(
            &F::operator())>::value>::type>
    : call_traits<decltype(&F::operator())>
{
};

// is_invocable

template<class T, class R, class ArgList, class = void>
struct is_invocable_impl : std::false_type {};

template<class T, class R, class... Args>
struct is_invocable_impl<
    T, R, mp11::mp_list<Args...>,
    std::void_t<decltype(std::declval<T>()(
        std::declval<Args>()...))>>
    : std::integral_constant<bool,
        call_traits<T>::value &&
        std::is_convertible<decltype(std::declval<T>()(
            std::declval<Args>()...)), R>::value>
{
};

template<class T, class R, class... Args>
struct is_invocable
    : is_invocable_impl<T, R, mp11::mp_list<Args...>>
{
};

} // detail
} // capy
} // boost

#endif
