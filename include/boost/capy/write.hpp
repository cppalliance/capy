//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_WRITE_HPP
#define BOOST_CAPY_WRITE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/consuming_buffers.hpp>
#include <boost/capy/concept/write_sink.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>

namespace boost {
namespace capy {

/** Write data until the buffer sequence is empty or an error occurs.

    This function writes data from the buffer sequence to the stream
    until either the entire buffer sequence is written or an error
    occurs.

    @tparam Stream The stream type, must satisfy @ref WriteStream.
    @tparam CB The buffer sequence type, must satisfy
        @ref ConstBufferSequence.

    @param stream The stream to write to.
    @param buffers The buffer sequence to write from.

    @return A task that yields `(system::error_code, std::size_t)`.
        On success, `ec` is default-constructed (no error) and `n` is
        `buffer_size(buffers)`. On error, `ec` contains the error code
        and `n` is the total number of bytes written before the error.

    @par Example
    @code
    task<void> example(WriteStream auto& stream)
    {
        std::string data = "Hello, World!";
        auto [ec, n] = co_await write(stream, const_buffer(data.data(), data.size()));
        if (ec)
        {
            // Handle error
        }
        // n bytes were written (n == data.size() on success)
    }
    @endcode

    @see WriteStream, ConstBufferSequence
*/
auto
write(
    WriteStream auto& stream,
    ConstBufferSequence auto const& buffers) ->
        task<io_result<std::size_t>>
{
    consuming_buffers consuming(buffers);
    std::size_t const total_size = buffer_size(buffers);
    std::size_t total_written = 0;

    while(total_written < total_size)
    {
        auto [ec, n] = co_await stream.write_some(consuming);
        if(ec)
            co_return {ec, total_written};
        consuming.consume(n);
        total_written += n;
    }

    co_return {{}, total_written};
}

/** Write data from a buffer sequence to a sink.

    This function writes data from the buffer sequence to the sink.
    The sink consumes all data from the provided buffers in a single
    operation.

    @note This function does not call `write_eof()` on the sink. The
        caller is responsible for signaling end-of-data when appropriate.

    @tparam Sink The sink type, must satisfy @ref WriteSink.
    @tparam CB The buffer sequence type, must satisfy
        @ref ConstBufferSequence.

    @param sink The sink to write to.
    @param buffers The buffer sequence to write from.

    @return A task that yields `(system::error_code, std::size_t)`.
        On success, `ec` is default-constructed (no error) and `n` is
        `buffer_size(buffers)`. On error, `ec` contains the error code
        and `n` is 0.

    @par Example
    @code
    task<void> example(WriteSink auto& sink)
    {
        std::string data = "Hello, World!";
        auto [ec, n] = co_await write(sink, const_buffer(data.data(), data.size()));
        if (ec)
        {
            // Handle error
        }
        // n bytes were written (n == data.size() on success)

        // Signal end of data when done
        auto [ec2] = co_await sink.write_eof();
    }
    @endcode

    @see WriteSink, ConstBufferSequence
*/
auto
write(
    WriteSink auto& sink,
    ConstBufferSequence auto const& buffers) ->
        task<io_result<std::size_t>>
{
    std::size_t const total_size = buffer_size(buffers);
    auto [ec] = co_await sink.write(buffers);
    if(ec.failed())
        co_return {ec, 0};
    co_return {{}, total_size};
}

} // namespace capy
} // namespace boost

#endif
