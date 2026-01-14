//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/brotli/encode.hpp>
#include <boost/capy/core/polystore.hpp>

#include <brotli/encode.h>

namespace boost {
namespace capy {
namespace brotli {

class encode_service_impl
    : public encode_service
{
public:
    using key_type = encode_service;

    explicit
    encode_service_impl(
        capy::polystore&) noexcept
    {
    }

    ~encode_service_impl()
    {
    }

    bool
    set_parameter(
        encoder_state* state,
        encoder_parameter param,
        std::uint32_t value) const noexcept override
    {
        return BrotliEncoderSetParameter(
            reinterpret_cast<BrotliEncoderState*>(state),
            static_cast<BrotliEncoderParameter>(param),
            value);
    }

    encoder_state*
    create_instance(alloc_func alloc_func, free_func free_func, void* opaque)
        const noexcept override
    {
        return reinterpret_cast<encoder_state*>(
            BrotliEncoderCreateInstance(
                alloc_func,
                free_func,
                opaque));
    }

    void
    destroy_instance(encoder_state* state) const noexcept override
    {
        BrotliEncoderDestroyInstance(
            reinterpret_cast<BrotliEncoderState*>(state));
    }

    std::size_t
    max_compressed_size(std::size_t input_size) const noexcept override
    {
        return BrotliEncoderMaxCompressedSize(input_size);
    }

    bool
    compress(
        int quality,
        int lgwin,
        encoder_mode mode,
        std::size_t input_size,
        const std::uint8_t input_buffer[],
        std::size_t* encoded_size,
        std::uint8_t encoded_buffer[]) const noexcept override
    {
        return BrotliEncoderCompress(
            quality,
            lgwin,
            static_cast<BrotliEncoderMode>(mode),
            input_size,
            input_buffer,
            encoded_size,
            encoded_buffer);
    }

    bool
    compress_stream(
        encoder_state* state,
        encoder_operation op,
        std::size_t* available_in,
        const std::uint8_t** next_in,
        std::size_t* available_out,
        std::uint8_t** next_out,
        std::size_t* total_out) const noexcept override
    {
        return BrotliEncoderCompressStream(
            reinterpret_cast<BrotliEncoderState*>(state),
            static_cast<BrotliEncoderOperation>(op),
            available_in,
            next_in,
            available_out,
            next_out,
            total_out);
    }

    bool
    is_finished(encoder_state* state) const noexcept override
    {
        return BrotliEncoderIsFinished(
            reinterpret_cast<BrotliEncoderState*>(state));
    }

    bool
    has_more_output(encoder_state* state) const noexcept override
    {
        return BrotliEncoderHasMoreOutput(
            reinterpret_cast<BrotliEncoderState*>(state));
    }

    const std::uint8_t*
    take_output(encoder_state* state, std::size_t* size) const noexcept override
    {
        return BrotliEncoderTakeOutput(
            reinterpret_cast<BrotliEncoderState*>(state),
            size);
    }

    std::uint32_t
    version() const noexcept override
    {
        return BrotliEncoderVersion();
    }

#if 0
    encoder_prepared_dictionary*
    prepare_dictionary(
        shared_dictionary_type type,
        std::size_t data_size,
        const std::uint8_t data[],
        int quality,
        alloc_func alloc_func,
        free_func free_func,
        void* opaque) const noexcept override
    {
        return reinterpret_cast<encoder_prepared_dictionary*>(
            BrotliEncoderPrepareDictionary(
                static_cast<BrotliSharedDictionaryType>(type),
                data_size,
                data,
                quality,
                alloc_func,
                free_func,
                opaque));
    }

    void
    destroy_prepared_dictionary(
        encoder_prepared_dictionary* dictionary) const noexcept override
    {
        BrotliEncoderDestroyPreparedDictionary(
            reinterpret_cast<BrotliEncoderPreparedDictionary*>(dictionary));
    }

    bool
    attach_prepared_dictionary(
        encoder_state* state,
        const encoder_prepared_dictionary* dictionary) const noexcept override
    {
        return BrotliEncoderAttachPreparedDictionary(
            reinterpret_cast<BrotliEncoderState*>(state),
            reinterpret_cast<const BrotliEncoderPreparedDictionary*>(dictionary));
    }

    std::size_t
    estimate_peak_memory_usage(int quality, int lgwin, std::size_t input_size)
        const noexcept override
    {
        return BrotliEncoderEstimatePeakMemoryUsage(
            quality,
            lgwin,
            input_size);
    }

    std::size_t
    get_prepared_dictionary_size(
        const encoder_prepared_dictionary* dictionary) const noexcept override
    {
        return BrotliEncoderGetPreparedDictionarySize(
            reinterpret_cast<const BrotliEncoderPreparedDictionary*>(dictionary));
    }
#endif
};

encode_service&
install_encode_service(polystore& ctx)
{
    return ctx.emplace<encode_service_impl>(ctx);
}

} // brotli
} // capy
} // boost
