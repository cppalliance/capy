//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_SPAN_LIKE_HPP
#define BOOST_CAPY_DETAIL_SPAN_LIKE_HPP

#include <boost/capy/detail/config.hpp>
#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/// A type with span-like subspan semantics
template<class T>
concept SpanLike = requires(T const& t) {
    { t.subspan(std::size_t{}, std::size_t{}) };
};

} // capy
} // boost

#endif
