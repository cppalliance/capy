//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_DETAIL_IS_SPAN_HPP
#define BOOST_CAPY_BUFFERS_DETAIL_IS_SPAN_HPP

#include <boost/capy/buffers/detail/config.hpp>
#include <type_traits>

namespace boost {
namespace capy {
namespace buffers {
namespace detail {

// Alias for true_type if T is a span-like type
template<class T, class = void>
struct is_span : std::false_type {};

template<class T>
struct is_span<T, detail::void_t<
    decltype(std::declval<T const&>().subspan(
        std::size_t(0), std::size_t(0)))
    > > : std::true_type
{
};

} // detail
} // buffers
} // capy
} // boost

#endif
