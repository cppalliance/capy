//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_PUSH_TO_HPP
#define BOOST_CAPY_IO_PUSH_TO_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/buffer_source.hpp>
#include <boost/capy/concept/write_sink.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>

#include <cstddef>
#include <span>

namespace boost {
namespace capy {

/** Transfer data from a BufferSource to a WriteSink.

    This function pulls data from the source and writes it to the
    sink until the source is exhausted or an error occurs. When
    the source signals completion, `write_eof()` is called on the
    sink to finalize the transfer.

    @tparam Src The source type, must satisfy @ref BufferSource.
    @tparam Sink The sink type, must satisfy @ref WriteSink.

    @param source The source to pull data from.
    @param sink The sink to write data to.

    @return A task that yields `(system::error_code, std::size_t)`.
        On success, `ec` is default-constructed (no error) and `n` is
        the total number of bytes transferred. On error, `ec` contains
        the error code and `n` is the total number of bytes transferred
        before the error.

    @par Example
    @code
    task<void> transfer_body(BufferSource auto& source, WriteSink auto& sink)
    {
        auto [ec, n] = co_await push_to(source, sink);
        if (ec.failed())
        {
            // Handle error
        }
        // n bytes were transferred
    }
    @endcode

    @see BufferSource, WriteSink
*/
template<BufferSource Src, WriteSink Sink>
task<io_result<std::size_t>>
push_to(Src& source, Sink& sink)
{
    static constexpr std::size_t max_bufs = 16;
    const_buffer arr[max_bufs];
    std::size_t total = 0;

    for(;;)
    {
        auto [ec, count] = co_await source.pull(arr, max_bufs);
        if(ec.failed())
            co_return {ec, total};

        if(count == 0)
        {
            auto [eof_ec] = co_await sink.write_eof();
            co_return {eof_ec, total};
        }

        std::span<const_buffer const> bufs(arr, count);
        auto [write_ec, n] = co_await sink.write(bufs);
        total += n;
        if(write_ec.failed())
            co_return {write_ec, total};
    }
}

/** Transfer data from a BufferSource to a WriteStream.

    This function pulls data from the source and writes it to the
    stream until the source is exhausted or an error occurs. The
    stream uses `write_some()` which may perform partial writes,
    so this function loops to ensure all data from each pull is
    written before requesting more from the source.

    Unlike the WriteSink overload, this function does not signal
    EOF explicitly since WriteStream does not provide a write_eof
    method. The transfer completes when the source is exhausted.

    @tparam Src The source type, must satisfy @ref BufferSource.
    @tparam Stream The stream type, must satisfy @ref WriteStream.

    @param source The source to pull data from.
    @param stream The stream to write data to.

    @return A task that yields `(system::error_code, std::size_t)`.
        On success, `ec` is default-constructed (no error) and `n` is
        the total number of bytes transferred. On error, `ec` contains
        the error code and `n` is the total number of bytes transferred
        before the error.

    @par Example
    @code
    task<void> transfer_body(BufferSource auto& source, WriteStream auto& stream)
    {
        auto [ec, n] = co_await push_to(source, stream);
        if (ec.failed())
        {
            // Handle error
        }
        // n bytes were transferred
    }
    @endcode

    @see BufferSource, WriteStream, pull_from
*/
template<BufferSource Src, WriteStream Stream>
task<io_result<std::size_t>>
push_to(Src& source, Stream& stream)
{
    static constexpr std::size_t max_bufs = 16;
    const_buffer arr[max_bufs];
    std::size_t total = 0;

    for(;;)
    {
        // Pull buffers from the source
        auto [ec, count] = co_await source.pull(arr, max_bufs);
        if(ec.failed())
            co_return {ec, total};

        // If source is exhausted, we're done
        if(count == 0)
            co_return {{}, total};

        // Write all data from this pull (handle partial writes)
        std::span<const_buffer const> bufs(arr, count);
        std::size_t remaining = buffer_size(bufs);
        std::size_t offset = 0;

        while(remaining > 0)
        {
            // WriteStream may accept less than provided
            auto [write_ec, n] = co_await stream.write_some(bufs);
            if(write_ec.failed())
                co_return {write_ec, total};

            total += n;
            remaining -= n;
            offset += n;

            // Advance the buffer span past what was written
            if(remaining > 0)
            {
                std::size_t to_consume = offset;
                std::size_t buf_idx = 0;

                // Find the starting point for the next write
                while(buf_idx < count && to_consume > 0)
                {
                    std::size_t buf_size = bufs[buf_idx].size();
                    if(to_consume >= buf_size)
                    {
                        to_consume -= buf_size;
                        ++buf_idx;
                    }
                    else
                    {
                        // Partial consumption of current buffer
                        arr[buf_idx] = const_buffer(
                            static_cast<char const*>(arr[buf_idx].data()) + to_consume,
                            arr[buf_idx].size() - to_consume);
                        to_consume = 0;
                    }
                }

                bufs = std::span<const_buffer const>(arr + buf_idx, count - buf_idx);
                offset = 0;
            }
        }
    }
}

} // namespace capy
} // namespace boost

#endif
