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
    statistics, and internal state. Applications provide input
    data through next_in/avail_in and receive output through
    next_out/avail_out. The service updates these fields as
    data is processed.

    Before use, initialize zalloc, zfree, and opaque. Set them
    to nullptr to use the default allocator. The state field
    is managed internally and should not be modified.

    @code
    // Example: Initialize a stream for compression
    boost::capy::zlib::stream st = {};
    st.zalloc = nullptr;  // Use default allocator
    st.zfree = nullptr;
    st.opaque = nullptr;

    // Set up input and output buffers
    st.next_in = input_buffer;
    st.avail_in = input_size;
    st.next_out = output_buffer;
    st.avail_out = output_size;

    // After deflate/inflate operations:
    // - next_in and next_out are updated to point past processed data
    // - avail_in and avail_out are decreased by bytes processed
    // - total_in and total_out track cumulative totals
    @endcode
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
