//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_TYPE_TRAITS_HPP
#define BOOST_CAPY_DETAIL_TYPE_TRAITS_HPP

#include <boost/capy/detail/config.hpp>

#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

template<class Base, class Derived>
using derived_from = std::integral_constant<bool,
    std::is_base_of<Base, Derived>::value &&
    std::is_convertible<
        Derived const volatile*,
        Base const volatile*>::value>;

template<bool...> struct bool_pack {};
template<bool... Bs>
struct all_true : std::is_same<bool_pack<
    true, Bs...>, bool_pack<Bs..., true>> {};

} // detail
} // capy
} // boost

#endif
