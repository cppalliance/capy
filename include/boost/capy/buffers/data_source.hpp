//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_DATA_SOURCE_HPP
#define BOOST_CAPY_BUFFERS_DATA_SOURCE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers/detail/except.hpp>
#include <boost/capy/concept/data_source.hpp>
#include <boost/system/error_code.hpp>

namespace boost {
namespace capy {
namespace buffers {

/** Metafunction to detect if a type is a data source.
*/
template<class T>
struct is_data_source
    : std::bool_constant<data_source<T>>
{
};

} // buffers
} // capy
} // boost

#endif
