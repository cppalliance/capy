//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ZLIB_COMPRESSION_METHOD_HPP
#define BOOST_CAPY_ZLIB_COMPRESSION_METHOD_HPP

#include <boost/capy/detail/config.hpp>

namespace boost {
namespace capy {
namespace zlib {

/** Compression method constants.

    Specifies the compression algorithm to use.
*/
enum compression_method
{
    /** The deflate compression method. */
    deflated = 8
};

} // zlib
} // capy
} // boost

#endif
