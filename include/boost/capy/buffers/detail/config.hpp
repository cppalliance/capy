//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_DETAIL_CONFIG_HPP
#define BOOST_CAPY_BUFFERS_DETAIL_CONFIG_HPP

#include <boost/capy/detail/config.hpp>

namespace boost {
namespace capy {
namespace buffers {

//-----------------------------------------------

// avoid all of Boost.TypeTraits for just this
namespace detail {
template<class...> struct make_void { typedef void type; };
template<class... Ts> using void_t = typename make_void<Ts...>::type;
} // detail

} // buffers
} // capy
} // boost

#endif
