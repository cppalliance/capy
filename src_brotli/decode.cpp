//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/brotli/decode.hpp>
#include <boost/capy/core/polystore.hpp>

#include <brotli/decode.h>

namespace boost {
namespace capy {
namespace brotli {

class decode_service_impl
    : public decode_service
{
public:
    using key_type = decode_service;

    explicit
    decode_service_impl(
        capy::polystore&) noexcept
    {
    }

    ~decode_service_impl()
    {
    }

    bool
    set_parameter(
        decoder_state* state,
        decoder_param param,
        std::uint32_t value) const noexcept override
    {
        return BrotliDecoderSetParameter(
            reinterpret_cast<BrotliDecoderState*>(state),
            static_cast<BrotliDecoderParameter>(param),
            value);
    }

    decoder_state*
    create_instance(alloc_func alloc_func, free_func free_func, void* opaque)
        const noexcept override
    {
        return reinterpret_cast<decoder_state*>(
            BrotliDecoderCreateInstance(
                alloc_func,
                free_func,
                opaque));
    }

    void
    destroy_instance(decoder_state* state) const noexcept override
    {
        BrotliDecoderDestroyInstance(
            reinterpret_cast<BrotliDecoderState*>(state));
    }

    decoder_result
    decompress(
        std::size_t encoded_size,
        const std::uint8_t encoded_buffer[],
        std::size_t* decoded_size,
        std::uint8_t decoded_buffer[]) const noexcept override
    {
        return static_cast<decoder_result>(
            BrotliDecoderDecompress(
                encoded_size,
                encoded_buffer,
                decoded_size,
                decoded_buffer));
    }

    decoder_result
    decompress_stream(
        decoder_state* state,
        std::size_t* available_in,
        const std::uint8_t** next_in,
        std::size_t* available_out,
        std::uint8_t** next_out,
        std::size_t* total_out) const noexcept override
    {
        return static_cast<decoder_result>(
            BrotliDecoderDecompressStream(
                reinterpret_cast<BrotliDecoderState*>(state),
                available_in,
                next_in,
                available_out,
                next_out,
                total_out));
    }

    bool
    has_more_output(const decoder_state* state) const noexcept override
    {
        return BrotliDecoderHasMoreOutput(
            reinterpret_cast<const BrotliDecoderState*>(state));
    }

    const std::uint8_t*
    take_output(decoder_state* state, std::size_t* size) const noexcept override
    {
        return BrotliDecoderTakeOutput(
            reinterpret_cast<BrotliDecoderState*>(state),
            size);
    }

    bool
    is_used(const decoder_state* state) const noexcept override
    {
        return BrotliDecoderIsUsed(
            reinterpret_cast<const BrotliDecoderState*>(state));
    }

    bool
    is_finished(const decoder_state* state) const noexcept override
    {
        return BrotliDecoderIsFinished(
            reinterpret_cast<const BrotliDecoderState*>(state));
    }

    error
    get_error_code(const decoder_state* state) const noexcept override
    {
        return static_cast<error>(
            BrotliDecoderGetErrorCode(
                reinterpret_cast<const BrotliDecoderState*>(state)));
    }

    const char*
    error_string(error c) const noexcept override
    {
        return BrotliDecoderErrorString(
            static_cast<BrotliDecoderErrorCode>(c));
    }

    std::uint32_t
    version() const noexcept override
    {
        return BrotliDecoderVersion();
    }

#if 0
    bool
    attach_dictionary(
        decoder_state* state,
        shared_dictionary_type type,
        std::size_t data_size,
        const std::uint8_t data[]) const noexcept override
    {
        return  BrotliDecoderAttachDictionary(
            reinterpret_cast<BrotliDecoderState*>(state),
            static_cast<BrotliSharedDictionaryType>(type),
            data_size,
            data);
    }

    void
    set_metadata_callbacks(
        decoder_state* state,
        metadata_start_func start_func,
        metadata_chunk_func chunk_func,
        void* opaque) const noexcept override
    {
        BrotliDecoderSetMetadataCallbacks(
            reinterpret_cast<BrotliDecoderState*>(state),
            start_func,
            chunk_func,
            opaque);
    }
#endif
};

decode_service&
install_decode_service(polystore& ctx)
{
    return ctx.emplace<decode_service_impl>(ctx);
}

} // brotli
} // capy
} // boost
