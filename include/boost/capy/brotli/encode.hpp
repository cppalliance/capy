//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BROTLI_ENCODE_HPP
#define BOOST_CAPY_BROTLI_ENCODE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/brotli/shared_dictionary.hpp>

#include <cstdint>

namespace boost {
namespace capy {
namespace brotli {

/** Opaque structure that holds encoder state. */
struct encoder_state;

/** Opaque type for pointer to different possible internal
    structures containing dictionary prepared for the encoder
*/
struct encoder_prepared_dictionary;

/** Encoder mode options.

    These values specify the type of input data for
    optimization purposes.
*/
enum class encoder_mode
{
    /** Generic mode for mixed or unknown data. */
    generic = 0,

    /** Mode optimized for UTF-8 text. */
    text    = 1,

    /** Mode optimized for WOFF 2.0 fonts. */
    font    = 2
};

/** Encoder stream operations.

    These operations control the streaming encoder behavior.
*/
enum class encoder_operation
{
    /** Process input data. */
    process       = 0,

    /** Flush pending output. */
    flush         = 1,

    /** Finish encoding and emit trailer. */
    finish        = 2,

    /** Emit metadata block. */
    emit_metadata = 3
};

/** Encoder parameter identifiers.

    These values identify parameters that can be set
    on an encoder instance.
*/
enum class encoder_parameter
{
    /** Encoder mode (generic, text, or font). */
    mode                             = 0,

    /** Compression quality (0-11). */
    quality                          = 1,

    /** Base-2 logarithm of window size. */
    lgwin                            = 2,

    /** Base-2 logarithm of input block size. */
    lgblock                          = 3,

    /** Disable literal context modeling flag. */
    disable_literal_context_modeling = 4,

    /** Expected input size hint. */
    size_hint                        = 5,

    /** Enable large window mode flag. */
    large_window                     = 6,

    /** Number of postfix bits for distance codes. */
    npostfix                         = 7,

    /** Number of direct distance codes. */
    ndirect                          = 8,

    /** Current stream offset. */
    stream_offset                    = 9
};

/** Brotli encoder constants.

    These constants define valid ranges and default values
    for encoder parameters.
*/
enum constants
{
    /** Minimum window size (2^10 bytes). */
    min_window_bits = 10,

    /** Maximum standard window size (2^24 bytes). */
    max_window_bits = 24,

    /** Maximum large window size (2^30 bytes). */
    large_max_window_bits = 30,

    /** Minimum input block size (2^16 bytes). */
    min_input_block_bits = 16,

    /** Maximum input block size (2^24 bytes). */
    max_input_block_bits = 24,

    /** Minimum quality level. */
    min_quality = 0,

    /** Maximum quality level. */
    max_quality = 11,

    /** Default quality level. */
    default_quality = 11,

    /** Default window size. */
    default_window  = 22,

    /** Default encoder mode. */
    default_mode    = 0
};

/** Provides the Brotli compression API.

    This service interface exposes Brotli encoder functionality
    through a set of virtual functions. The encoder can operate
    in one-shot mode for simple compression or streaming mode
    for processing data in chunks.

    The quality parameter ranges from 0 to 11 (see min_quality
    and max_quality constants). Quality 0 offers fastest compression
    with lower ratio, while quality 11 offers best compression with
    slower speed. The default quality is 11.

    @code
    // Example: Simple one-shot compression
    boost::capy::datastore ctx;
    auto& encoder = boost::capy::brotli::install_encode_service(ctx);

    std::vector<std::uint8_t> input_data = get_input_data();
    std::size_t max_size = encoder.max_compressed_size(input_data.size());
    std::vector<std::uint8_t> output(max_size);
    std::size_t encoded_size = max_size;

    bool success = encoder.compress(
        11,  // quality (0-11)
        22,  // lgwin (window size = 2^22)
        boost::capy::brotli::encoder_mode::generic,
        input_data.size(),
        input_data.data(),
        &encoded_size,
        output.data());

    if (success)
    {
        output.resize(encoded_size);
        // Use compressed data
    }
    @endcode

    @code
    // Example: Streaming compression
    auto* state = encoder.create_instance(nullptr, nullptr, nullptr);

    // Set parameters
    encoder.set_parameter(state,
        boost::capy::brotli::encoder_parameter::quality, 6);
    encoder.set_parameter(state,
        boost::capy::brotli::encoder_parameter::lgwin, 22);

    std::size_t available_in = input_data.size();
    const std::uint8_t* next_in = input_data.data();
    std::size_t available_out = output.size();
    std::uint8_t* next_out = output.data();
    std::size_t total_out = 0;

    bool success = encoder.compress_stream(
        state,
        boost::capy::brotli::encoder_operation::finish,
        &available_in,
        &next_in,
        &available_out,
        &next_out,
        &total_out);

    encoder.destroy_instance(state);
    @endcode
*/
struct BOOST_SYMBOL_VISIBLE
    encode_service
{
    /** Set an encoder parameter.
        @param state The encoder state.
        @param param The parameter identifier.
        @param value The parameter value.
        @return True on success, false on error.
    */
    virtual bool
    set_parameter(
        encoder_state* state,
        encoder_parameter param,
        std::uint32_t value) const noexcept = 0;

    /** Create a new encoder instance.
        @param alloc_func Allocation function.
        @param free_func Deallocation function.
        @param opaque Opaque pointer passed to allocation functions.
        @return Pointer to encoder state, or nullptr on error.
    */
    virtual encoder_state*
    create_instance(
        alloc_func alloc_func,
        free_func free_func,
        void* opaque) const noexcept = 0;

    /** Destroy an encoder instance.
        @param state The encoder state to destroy.
    */
    virtual void
    destroy_instance(encoder_state* state) const noexcept = 0;

    /** Return maximum possible compressed size.
        @param input_size The input data size.
        @return Maximum compressed size in bytes.
    */
    virtual std::size_t
    max_compressed_size(std::size_t input_size) const noexcept = 0;

    /** Compress data in one call.
        @param quality Compression quality (0-11).
        @param lgwin Base-2 logarithm of window size.
        @param mode Encoder mode.
        @param input_size Input data size.
        @param input_buffer Input data buffer.
        @param encoded_size Pointer to variable receiving output size.
        @param encoded_buffer Output buffer.
        @return True on success, false on error.
    */
    virtual bool
    compress(
        int quality,
        int lgwin,
        encoder_mode mode,
        std::size_t input_size,
        const std::uint8_t input_buffer[],
        std::size_t* encoded_size,
        std::uint8_t encoded_buffer[]) const noexcept = 0;

    /** Compress data in streaming mode.
        @param state The encoder state.
        @param op The encoder operation.
        @param available_in Pointer to input bytes available.
        @param next_in Pointer to pointer to input data.
        @param available_out Pointer to output space available.
        @param next_out Pointer to pointer to output buffer.
        @param total_out Pointer to variable receiving total bytes written.
        @return True on success, false on error.
    */
    virtual bool
    compress_stream(
        encoder_state* state,
        encoder_operation op,
        std::size_t* available_in,
        const std::uint8_t** next_in,
        std::size_t* available_out,
        std::uint8_t** next_out,
        std::size_t* total_out) const noexcept = 0;

    /** Check if encoding is finished.
        @param state The encoder state.
        @return True if encoding is complete, false otherwise.
    */
    virtual bool
    is_finished(encoder_state* state) const noexcept = 0;

    /** Check if more output is available.
        @param state The encoder state.
        @return True if output is available, false otherwise.
    */
    virtual bool
    has_more_output(encoder_state* state) const noexcept = 0;

    /** Return buffered output data.
        @param state The encoder state.
        @param size Pointer to variable receiving output size.
        @return Pointer to output buffer.
    */
    virtual const std::uint8_t*
    take_output(
        encoder_state* state,
        std::size_t* size) const noexcept = 0;

    /** Return the Brotli library version.
        @return Version number.
    */
    virtual std::uint32_t
    version() const noexcept = 0;

#if 0
    virtual encoder_prepared_dictionary*
    prepare_dictionary(
        shared_dictionary_type type,
        std::size_t data_size,
        const std::uint8_t data[],
        int quality,
        alloc_func alloc_func,
        free_func free_func,
        void* opaque) const noexcept = 0;

    virtual void
    destroy_prepared_dictionary(
        encoder_prepared_dictionary* dictionary) const noexcept = 0;

    virtual bool
    attach_prepared_dictionary(
        encoder_state* state,
        const encoder_prepared_dictionary* dictionary) const noexcept = 0;

    virtual std::size_t
    estimate_peak_memory_usage(
        int quality,
        int lgwin,
        std::size_t input_size) const noexcept = 0;

    virtual std::size_t
    get_prepared_dictionary_size(
        const encoder_prepared_dictionary* dictionary) const noexcept = 0;
#endif
};

/** Install the encode service into a polystore.
    @param ctx The polystore to install the service into.
    @return A reference to the installed encode service.
*/
BOOST_CAPY_DECL
encode_service&
install_encode_service(polystore& ctx);

} // brotli
} // capy
} // boost

#endif
