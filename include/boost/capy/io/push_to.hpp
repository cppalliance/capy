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
#include <boost/capy/cond.hpp>
#include <boost/capy/concept/buffer_source.hpp>
#include <boost/capy/concept/write_sink.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/io_task.hpp>

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

    @return A task that yields `(std::error_code, std::size_t)`.
        On success, `ec` is default-constructed (no error) and `n` is
        the total number of bytes transferred. On error, `ec` contains
        the error code and `n` is the total number of bytes transferred
        before the error.

    @par Example
    @code
    task<void> transfer_body(BufferSource auto& source, WriteSink auto& sink)
    {
        auto [ec, n] = co_await push_to(source, sink);
        if (ec)
        {
            // Handle error
        }
        // n bytes were transferred
    }
    @endcode

    @see BufferSource, WriteSink
*/
template<BufferSource Src, WriteSink Sink>
io_task<std::size_t>
push_to(Src& source, Sink& sink)
{
    const_buffer arr[detail::max_iovec_];
    std::size_t total = 0;

    for(;;)
    {
        auto [ec, bufs] = co_await source.pull(arr);
        if(ec == cond::eof)
        {
            auto [eof_ec] = co_await sink.write_eof();
            co_return {eof_ec, total};
        }
        if(ec)
            co_return {ec, total};

        auto [write_ec, n] = co_await sink.write(bufs);
        total += n;
        source.consume(n);
        if(write_ec)
            co_return {write_ec, total};
    }
}

/** Transfer data from a BufferSource to a WriteStream.

    This function pulls data from the source and writes it to the
    stream until the source is exhausted or an error occurs. The
    stream uses `write_some()` which may perform partial writes,
    so this function loops until all pulled data is consumed.

    Unlike the WriteSink overload, this function does not signal
    EOF explicitly since WriteStream does not provide a write_eof
    method. The transfer completes when the source is exhausted.

    @tparam Src The source type, must satisfy @ref BufferSource.
    @tparam Stream The stream type, must satisfy @ref WriteStream.

    @param source The source to pull data from.
    @param stream The stream to write data to.

    @return A task that yields `(std::error_code, std::size_t)`.
        On success, `ec` is default-constructed (no error) and `n` is
        the total number of bytes transferred. On error, `ec` contains
        the error code and `n` is the total number of bytes transferred
        before the error.

    @par Example
    @code
    task<void> transfer_body(BufferSource auto& source, WriteStream auto& stream)
    {
        auto [ec, n] = co_await push_to(source, stream);
        if (ec)
        {
            // Handle error
        }
        // n bytes were transferred
    }
    @endcode

    @see BufferSource, WriteStream, pull_from
*/
template<BufferSource Src, WriteStream Stream>
io_task<std::size_t>
push_to(Src& source, Stream& stream)
{
    const_buffer arr[detail::max_iovec_];
    std::size_t total = 0;

    for(;;)
    {
        auto [ec, bufs] = co_await source.pull(arr);
        if(ec == cond::eof)
            co_return {{}, total};
        if(ec)
            co_return {ec, total};

        auto [write_ec, n] = co_await stream.write_some(bufs);
        if(write_ec)
            co_return {write_ec, total};

        total += n;
        source.consume(n);
    }
}

} // namespace capy
} // namespace boost

#endif
