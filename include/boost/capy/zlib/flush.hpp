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

    Use no_flush for maximum compression efficiency when more
    input is available. Use sync_flush to force output to a
    byte boundary, allowing partial decompression. Use finish
    when no more input is available to complete compression
    and write the trailer.

    @code
    // Example: Streaming compression with periodic flushing
    boost::capy::zlib::stream st = {};
    // ... initialize stream ...

    while (has_more_data)
    {
        st.next_in = get_next_chunk();
        st.avail_in = chunk_size;

        // Flush at chunk boundaries for progressive decoding
        int flush_mode = has_more_data ?
            boost::capy::zlib::sync_flush :
            boost::capy::zlib::finish;

        deflate_svc.deflate(st, flush_mode);
    }
    @endcode
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
