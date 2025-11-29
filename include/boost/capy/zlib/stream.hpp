//
// Copyright (c) 2021 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ZLIB_STREAM_HPP
#define BOOST_CAPY_ZLIB_STREAM_HPP

namespace boost {
namespace capy {
namespace zlib {

/** ZLib stream state structure.

    This structure maintains the state for compression and
    decompression operations, including input/output buffers,
    statistics, and internal state.
*/
struct stream
{
    /** Allocating function pointer type. */
    using alloc_func = void*(*)(void*, unsigned int, unsigned int);

    /** Deallocating function pointer type. */
    using free_func = void(*)(void*, void*);

    /** Pointer to next input byte. */
    unsigned char* next_in;

    /** Number of bytes available at next_in. */
    unsigned int   avail_in;

    /** Total number of input bytes read so far. */
    unsigned long  total_in;

    /** Pointer where next output byte will be placed. */
    unsigned char* next_out;

    /** Remaining free space at next_out. */
    unsigned int   avail_out;

    /** Total number of bytes output so far. */
    unsigned long  total_out;

    /** Last error message, NULL if no error. */
    char*          msg;

    /** Internal state, not visible to applications. */
    void*          state;

    /** Function used to allocate internal state. */
    alloc_func     zalloc;

    /** Function used to deallocate internal state. */
    free_func      zfree;

    /** Private data object passed to zalloc and zfree. */
    void*          opaque;

    /** Best guess about data type (binary or text for deflate, decoding state for inflate). */
    int            data_type;

    /** Adler-32 or CRC-32 value of the uncompressed data. */
    unsigned long  adler;

    /** Reserved for future use. */
    unsigned long  reserved;
};

} // zlib
} // capy
} // boost

#endif
