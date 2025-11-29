//
// Copyright (c) 2021 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ZLIB_INFLATE_HPP
#define BOOST_CAPY_ZLIB_INFLATE_HPP

#include <boost/capy/polystore_fwd.hpp>
#include <boost/capy/detail/config.hpp>
#include <boost/capy/zlib/stream.hpp>

namespace boost {
namespace capy {
namespace zlib {

/** Provides the ZLib decompression API.

    This service interface exposes the ZLib inflate (decompression)
    functionality through a set of virtual functions. The inflate
    algorithm reverses the deflate compression, restoring the
    original uncompressed data.

    The windowBits parameter in init2() controls format detection:
    - 8..15: zlib format with specified window size
    - -8..-15: raw deflate format (no header/trailer)
    - 16+windowBits: gzip format only
    - 32+windowBits: auto-detect zlib or gzip format

    @code
    // Example: Basic decompression
    boost::capy::datastore ctx;
    auto& inflate_svc = boost::capy::zlib::install_inflate_service(ctx);

    boost::capy::zlib::stream st = {};
    std::vector<unsigned char> compressed_data = get_compressed();
    std::vector<unsigned char> output(1024 * 1024); // 1MB buffer

    st.zalloc = nullptr;
    st.zfree = nullptr;
    st.opaque = nullptr;

    inflate_svc.init(st);

    st.avail_in = compressed_data.size();
    st.next_in = compressed_data.data();
    st.avail_out = output.size();
    st.next_out = output.data();

    inflate_svc.inflate(st, boost::capy::zlib::finish);
    output.resize(st.total_out);

    inflate_svc.inflate_end(st);
    @endcode

    @code
    // Example: Auto-detect gzip or zlib format
    boost::capy::zlib::stream st = {};
    st.zalloc = nullptr;
    st.zfree = nullptr;

    // Auto-detect format (32 + 15 for max window)
    inflate_svc.init2(st, 32 + 15);

    st.avail_in = compressed_data.size();
    st.next_in = compressed_data.data();
    st.avail_out = output.size();
    st.next_out = output.data();

    int result = inflate_svc.inflate(st, boost::capy::zlib::no_flush);
    // Handle result...

    inflate_svc.inflate_end(st);
    @endcode
*/
struct BOOST_SYMBOL_VISIBLE
    inflate_service
{
    /** Return the ZLib version string. */
    virtual char const* version() const noexcept = 0;

    /** Initialize inflate decompression.
        @param st The stream to initialize.
        @return Zero on success, or an error code.
    */
    virtual int init(stream& st) const = 0;

    /** Initialize inflate decompression with extended parameters.
        @param st The stream to initialize.
        @param windowBits The base-2 logarithm of the window size.
        @return Zero on success, or an error code.
    */
    virtual int init2(stream& st, int windowBits) const = 0;

    /** Decompress data in the stream.
        @param st The stream containing data to decompress.
        @param flush The flush mode.
        @return Status code indicating decompression state.
    */
    virtual int inflate(stream& st, int flush) const = 0;

    /** Release all resources held by the inflate stream.
        @param st The stream to finalize.
        @return Zero on success, or an error code.
    */
    virtual int inflate_end(stream& st) const = 0;

    /** Set the decompression dictionary.
        @param st The stream.
        @param dict Pointer to the dictionary data.
        @param len Length of the dictionary.
        @return Zero on success, or an error code.
    */
    virtual int set_dict(stream& st, unsigned char const* dict, unsigned len) const = 0;

    /** Return the current decompression dictionary.
        @param st The stream.
        @param dest Destination buffer for the dictionary.
        @param len Pointer to variable receiving dictionary length.
        @return Zero on success, or an error code.
    */
    virtual int get_dict(stream& st, unsigned char* dest, unsigned* len) const = 0;

    /** Synchronize the decompression state.
        @param st The stream to synchronize.
        @return Zero on success, or an error code.
    */
    virtual int sync(stream& st) const = 0;

    /** Duplicate an inflate stream.
        @param dest The destination stream.
        @param src The source stream to duplicate.
        @return Zero on success, or an error code.
    */
    virtual int dup(stream& dest, stream& src) const = 0;

    /** Reset the inflate stream state.
        @param st The stream to reset.
        @return Zero on success, or an error code.
    */
    virtual int reset(stream& st) const = 0;

    /** Reset the inflate stream state with new window size.
        @param st The stream to reset.
        @param windowBits The base-2 logarithm of the window size.
        @return Zero on success, or an error code.
    */
    virtual int reset2(stream& st, int windowBits) const = 0;

    /** Insert bits into the input stream.
        @param st The stream.
        @param bits Number of bits to insert.
        @param value The bit pattern to insert.
        @return Zero on success, or an error code.
    */
    virtual int prime(stream& st, int bits, int value) const = 0;

    /** Return the current inflate mark.
        @param st The stream.
        @return The mark value, or -1 on error.
    */
    virtual long mark(stream& st) const = 0;

    /** Return the gzip header information.
        @param st The stream.
        @param header Pointer to gzip header structure.
        @return Zero on success, or an error code.
    */
    virtual int get_header(stream& st, void* header) const = 0;

    /** Initialize backward inflate decompression.
        @param st The stream to initialize.
        @param windowBits The base-2 logarithm of the window size.
        @param window Pointer to the window buffer.
        @return Zero on success, or an error code.
    */
    virtual int back_init(stream& st, int windowBits, unsigned char* window) const = 0;

    /** Release resources held by backward inflate stream.
        @param st The stream to finalize.
        @return Zero on success, or an error code.
    */
    virtual int back_end(stream& st) const = 0;

    /** Return ZLib compile-time flags.
        @return Bit flags indicating compile-time options.
    */
    virtual unsigned long compile_flags() const = 0;
};

/** Install the inflate service into a polystore.
    @param ctx The polystore to install the service into.
    @return A reference to the installed inflate service.
*/
BOOST_CAPY_DECL
inflate_service&
install_inflate_service(polystore& ctx);

} // zlib
} // capy
} // boost

#endif
