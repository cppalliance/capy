//
// Copyright (c) 2021 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ZLIB_DEFLATE_HPP
#define BOOST_CAPY_ZLIB_DEFLATE_HPP

#include <boost/capy/polystore_fwd.hpp>
#include <boost/capy/detail/config.hpp>
#include <boost/capy/zlib/stream.hpp>

#include <cstddef>

namespace boost {
namespace capy {
namespace zlib {

/** Provides the ZLib compression API.

    This service interface exposes the ZLib deflate (compression)
    functionality through a set of virtual functions. The deflate
    algorithm compresses data by finding repeated byte sequences
    and encoding them efficiently using a combination of LZ77 and
    Huffman coding.

    The windowBits parameter in init2() controls the format:
    - 8..15: zlib format with specified window size
    - -8..-15: raw deflate format (no header/trailer)
    - 16+windowBits: gzip format

    @code
    // Example: Basic compression
    boost::capy::datastore ctx;
    auto& deflate_svc = boost::capy::zlib::install_deflate_service(ctx);

    boost::capy::zlib::stream st = {};
    std::vector<unsigned char> input_data = get_data();
    std::vector<unsigned char> output(input_data.size() * 2);

    st.zalloc = nullptr;
    st.zfree = nullptr;
    st.opaque = nullptr;

    deflate_svc.init(st, boost::capy::zlib::default_compression);

    st.avail_in = input_data.size();
    st.next_in = input_data.data();
    st.avail_out = output.size();
    st.next_out = output.data();

    deflate_svc.deflate(st, boost::capy::zlib::finish);
    output.resize(st.total_out);

    deflate_svc.deflate_end(st);
    @endcode

    @code
    // Example: Gzip compression with custom window size
    boost::capy::zlib::stream st = {};
    st.zalloc = nullptr;
    st.zfree = nullptr;

    // Use gzip format (16 + 15 for max window)
    deflate_svc.init2(st,
        6,                                      // level
        boost::capy::zlib::deflated,           // method
        16 + 15,                                // gzip format
        8,                                      // memLevel
        boost::capy::zlib::default_strategy);   // strategy

    // Compress data...
    deflate_svc.deflate_end(st);
    @endcode
*/
struct BOOST_SYMBOL_VISIBLE
    deflate_service
{
    /** Return the ZLib version string. */
    virtual char const* version() const noexcept = 0;

    /** Initialize deflate compression.
        @param st The stream to initialize.
        @param level The compression level.
        @return Zero on success, or an error code.
    */
    virtual int init(stream& st, int level) const = 0;

    /** Initialize deflate compression with extended parameters.
        @param st The stream to initialize.
        @param level The compression level.
        @param method The compression method.
        @param windowBits The base-2 logarithm of the window size.
        @param memLevel Memory usage level (1-9).
        @param strategy The compression strategy.
        @return Zero on success, or an error code.
    */
    virtual int init2(stream& st, int level, int method,
        int windowBits, int memLevel, int strategy) const = 0;

    /** Set the compression dictionary.
        @param st The stream.
        @param dict Pointer to the dictionary data.
        @param len Length of the dictionary.
        @return Zero on success, or an error code.
    */
    virtual int set_dict(stream& st, unsigned char const* dict, unsigned len) const = 0;

    /** Return the current compression dictionary.
        @param st The stream.
        @param dest Destination buffer for the dictionary.
        @param len Pointer to variable receiving dictionary length.
        @return Zero on success, or an error code.
    */
    virtual int get_dict(stream& st, unsigned char* dest, unsigned* len) const = 0;

    /** Duplicate a deflate stream.
        @param dest The destination stream.
        @param src The source stream to duplicate.
        @return Zero on success, or an error code.
    */
    virtual int dup(stream& dest, stream& src) const = 0;

    /** Compress data in the stream.
        @param st The stream containing data to compress.
        @param flush The flush mode.
        @return Status code indicating compression state.
    */
    virtual int deflate(stream& st, int flush) const = 0;

    /** Release all resources held by the deflate stream.
        @param st The stream to finalize.
        @return Zero on success, or an error code.
    */
    virtual int deflate_end(stream& st) const = 0;

    /** Reset the deflate stream state.
        @param st The stream to reset.
        @return Zero on success, or an error code.
    */
    virtual int reset(stream& st) const = 0;

    /** Dynamically update compression parameters.
        @param st The stream.
        @param level The new compression level.
        @param strategy The new compression strategy.
        @return Zero on success, or an error code.
    */
    virtual int params(stream& st, int level, int strategy) const = 0;

    /** Return an upper bound on compressed size.
        @param st The stream.
        @param sourceLen The length of source data.
        @return Maximum possible compressed size.
    */
    virtual std::size_t bound(stream& st, unsigned long sourceLen) const = 0;

    /** Return the number of pending output bytes.
        @param st The stream.
        @param pending Pointer to variable receiving pending byte count.
        @param bits Pointer to variable receiving pending bit count.
        @return Zero on success, or an error code.
    */
    virtual int pending(stream& st, unsigned* pending, int* bits) const = 0;

    /** Insert bits into the compressed stream.
        @param st The stream.
        @param bits Number of bits to insert.
        @param value The bit pattern to insert.
        @return Zero on success, or an error code.
    */
    virtual int prime(stream& st, int bits, int value) const = 0;

    /** Set the gzip header information.
        @param st The stream.
        @param header Pointer to gzip header structure.
        @return Zero on success, or an error code.
    */
    virtual int set_header(stream& st, void* header) const = 0;
};

/** Install the deflate service into a polystore.
    @param ctx The polystore to install the service into.
    @return A reference to the installed deflate service.
*/
BOOST_CAPY_DECL
deflate_service&
install_deflate_service(polystore& ctx);

} // zlib
} // capy
} // boost

#endif
