//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ZLIB_COMPRESSION_STRATEGY_HPP
#define BOOST_CAPY_ZLIB_COMPRESSION_STRATEGY_HPP

namespace boost {
namespace capy {
namespace zlib {

/** Compression strategy constants.

    These values tune the compression algorithm for specific
    types of input data.
*/
enum compression_strategy
{
    /** Use the default compression strategy. */
    default_strategy = 0,

    /** Strategy optimized for data with small values. */
    filtered         = 1,

    /** Force Huffman encoding only (no string match). */
    huffman_only     = 2,

    /** Limit match distances to one (run-length encoding). */
    rle              = 3,

    /** Prevent use of dynamic Huffman codes. */
    fixed            = 4
};

} // zlib
} // capy
} // boost

#endif
