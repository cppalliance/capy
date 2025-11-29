//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BROTLI_DECODE_HPP
#define BOOST_CAPY_BROTLI_DECODE_HPP

#include <boost/capy/brotli/error.hpp>
#include <boost/capy/brotli/shared_dictionary.hpp>

#include <cstdint>

namespace boost {
namespace capy {
namespace brotli {

/** Opaque structure that holds decoder state. */
struct decoder_state;

/** Decoder result codes.

    These values indicate the result of decompression operations.
*/
enum class decoder_result
{
    /** Decompression error occurred. */
    error = 0,

    /** Decompression completed successfully. */
    success = 1,

    /** More input data is needed. */
    needs_more_input = 2,

    /** More output space is needed. */
    needs_more_output = 3
};

/** Decoder parameter identifiers.

    These values identify parameters that can be set
    on a decoder instance.
*/
enum class decoder_param
{
    /** Disable automatic ring buffer reallocation. */
    disable_ring_buffer_reallocation = 0,

    /** Enable large window mode. */
    large_window = 1
};

/** Callback to fire on metadata block start. */
using metadata_start_func = void (*)(void* opaque, std::size_t size);

/** Callback to fire on metadata block chunk becomes available. */
using metadata_chunk_func = void (*)(void* opaque, const std::uint8_t* data, std::size_t size);

/** Provides the Brotli decompression API.

    This service interface exposes Brotli decoder functionality
    through a set of virtual functions. The decoder can operate
    in one-shot mode for simple decompression or streaming mode
    for processing data in chunks.

    @code
    // Example: Simple one-shot decompression
    boost::capy::datastore ctx;
    auto& decoder = boost::capy::brotli::install_decode_service(ctx);

    std::vector<std::uint8_t> compressed_data = get_compressed_data();
    std::vector<std::uint8_t> output(1024 * 1024); // 1MB buffer
    std::size_t decoded_size = output.size();

    auto result = decoder.decompress(
        compressed_data.size(),
        compressed_data.data(),
        &decoded_size,
        output.data());

    if (result == boost::capy::brotli::decoder_result::success)
    {
        output.resize(decoded_size);
        // Use decompressed data
    }
    @endcode

    @code
    // Example: Streaming decompression
    auto* state = decoder.create_instance(nullptr, nullptr, nullptr);

    std::size_t available_in = compressed_data.size();
    const std::uint8_t* next_in = compressed_data.data();
    std::size_t available_out = output.size();
    std::uint8_t* next_out = output.data();
    std::size_t total_out = 0;

    auto result = decoder.decompress_stream(
        state,
        &available_in,
        &next_in,
        &available_out,
        &next_out,
        &total_out);

    decoder.destroy_instance(state);
    @endcode
*/
struct BOOST_SYMBOL_VISIBLE
    decode_service
{
    /** Set a decoder parameter.
        @param state The decoder state.
        @param param The parameter identifier.
        @param value The parameter value.
        @return True on success, false on error.
    */
    virtual bool
    set_parameter(
        decoder_state* state,
        decoder_param param,
        std::uint32_t value) const noexcept = 0;

    /** Create a new decoder instance.
        @param alloc_func Allocation function.
        @param free_func Deallocation function.
        @param opaque Opaque pointer passed to allocation functions.
        @return Pointer to decoder state, or nullptr on error.
    */
    virtual decoder_state*
    create_instance(
        alloc_func alloc_func,
        free_func free_func,
        void* opaque) const noexcept = 0;

    /** Destroy a decoder instance.
        @param state The decoder state to destroy.
    */
    virtual void
    destroy_instance(decoder_state* state) const noexcept = 0;

    /** Decompress data in one call.
        @param encoded_size Input data size.
        @param encoded_buffer Input data buffer.
        @param decoded_size Pointer to variable receiving output size.
        @param decoded_buffer Output buffer.
        @return Result code indicating success or failure.
    */
    virtual decoder_result
    decompress(
        std::size_t encoded_size,
        const std::uint8_t encoded_buffer[],
        std::size_t* decoded_size,
        std::uint8_t decoded_buffer[]) const noexcept = 0;

    /** Decompress data in streaming mode.
        @param state The decoder state.
        @param available_in Pointer to input bytes available.
        @param next_in Pointer to pointer to input data.
        @param available_out Pointer to output space available.
        @param next_out Pointer to pointer to output buffer.
        @param total_out Pointer to variable receiving total bytes written.
        @return Result code indicating decoder state.
    */
    virtual decoder_result
    decompress_stream(
        decoder_state* state,
        std::size_t* available_in,
        const std::uint8_t** next_in,
        std::size_t* available_out,
        std::uint8_t** next_out,
        std::size_t* total_out) const noexcept = 0;

    /** Check if more output is available.
        @param state The decoder state.
        @return True if output is available, false otherwise.
    */
    virtual bool
    has_more_output(const decoder_state* state) const noexcept = 0;

    /** Return buffered output data.
        @param state The decoder state.
        @param size Pointer to variable receiving output size.
        @return Pointer to output buffer.
    */
    virtual const std::uint8_t*
    take_output(decoder_state* state, std::size_t* size) const noexcept = 0;

    /** Check if decoder has been used.
        @param state The decoder state.
        @return True if decoder has processed data, false otherwise.
    */
    virtual bool
    is_used(const decoder_state* state) const noexcept = 0;

    /** Check if decompression is finished.
        @param state The decoder state.
        @return True if decompression is complete, false otherwise.
    */
    virtual bool
    is_finished(const decoder_state* state) const noexcept = 0;

    /** Return the error code from the decoder.
        @param state The decoder state.
        @return The error code.
    */
    virtual error
    get_error_code(const decoder_state* state) const noexcept = 0;

    /** Return a string description of an error code.
        @param c The error code.
        @return Pointer to error description string.
    */
    virtual const char*
    error_string(error c) const noexcept = 0;

    /** Return the Brotli library version.
        @return Version number.
    */
    virtual std::uint32_t
    version() const noexcept = 0;

#if 0
    virtual bool
    attach_dictionary(
        decoder_state* state,
        shared_dictionary_type type,
        std::size_t data_size,
        const std::uint8_t data[]) const noexcept = 0;

    virtual void
    set_metadata_callbacks(
        decoder_state* state,
        metadata_start_func start_func,
        metadata_chunk_func chunk_func,
        void* opaque) const noexcept = 0;
#endif
};

/** Install the decode service into a polystore.
    @param ctx The polystore to install the service into.
    @return A reference to the installed decode service.
*/
BOOST_CAPY_DECL
decode_service&
install_decode_service(polystore& ctx);

} // brotli
} // capy
} // boost

#endif
