//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ZLIB_DATA_TYPE_HPP
#define BOOST_CAPY_ZLIB_DATA_TYPE_HPP

namespace boost {
namespace capy {
namespace zlib {

/** Data type constants for the stream data_type field.

    These values represent the best guess about the type
    of data being compressed or decompressed.
*/
enum data_type
{
    /** Binary data. */
    binary  = 0,

    /** Text data. */
    text    = 1,

    /** ASCII text data (same as text). */
    ascii   = 1,

    /** Data type is unknown. */
    unknown = 2
};

} // zlib
} // capy
} // boost

#endif
