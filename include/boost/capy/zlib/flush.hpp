//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ZLIB_FLUSH_HPP
#define BOOST_CAPY_ZLIB_FLUSH_HPP

namespace boost {
namespace capy {
namespace zlib {

/** Flush method constants.

    These values control how and when compressed data is
    flushed from internal buffers during compression operations.
*/
enum flush
{
    /** No flushing, allow optimal compression. */
    no_flush      = 0,

    /** Flush to byte boundary (deprecated). */
    partial_flush = 1,

    /** Flush to byte boundary for synchronization. */
    sync_flush    = 2,

    /** Full flush, reset compression state. */
    full_flush    = 3,

    /** Finish compression, emit trailer. */
    finish        = 4,

    /** Flush current block to output. */
    block         = 5,

    /** Flush up to end of previous block. */
    trees         = 6
};

} // zlib
} // capy
} // boost

#endif
