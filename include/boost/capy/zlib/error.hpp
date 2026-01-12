//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ZLIB_ERROR_HPP
#define BOOST_CAPY_ZLIB_ERROR_HPP

#include <boost/capy/detail/config.hpp>

namespace boost {
namespace capy {
namespace zlib {

/** Error codes returned from compression/decompression functions.

    Negative values are errors, positive values are used
    for special but normal events.
*/
enum class error
{
    ok          =  0,
    stream_end  =  1,
    need_dict   =  2,
    errno_      = -1,
    stream_err  = -2,
    data_err    = -3,
    mem_err     = -4,
    buf_err     = -5,
    version_err = -6
};

} // zlib
} // capy
} // boost

#include <boost/capy/zlib/impl/error.hpp>

#endif
