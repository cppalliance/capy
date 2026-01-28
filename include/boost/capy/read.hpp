//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_READ_HPP
#define BOOST_CAPY_READ_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/consuming_buffers.hpp>
#include <boost/capy/concept/dynamic_buffer.hpp>
#include <boost/capy/concept/read_source.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <system_error>

#include <cstddef>

namespace boost {
namespace capy {

/** Read data until the buffer sequence is full or an error occurs.

    This function reads data from the stream into the buffer sequence
    until either the entire buffer sequence is filled or an error
    occurs (including end-of-file).

    @tparam Stream The stream type, must satisfy @ref ReadStream.
    @tparam MB The buffer sequence type, must satisfy
        @ref MutableBufferSequence.

    @param stream The stream to read from.
    @param buffers The buffer sequence to read into.

    @return A task that yields `(std::error_code, std::size_t)`.
        On success, `ec` is default-constructed (no error) and `n` is
        `buffer_size(buffers)`. On error or EOF, `ec` contains the
        error code and `n` is the total number of bytes written before
        the error.

    @par Example
    @code
    task<void> example(ReadStream auto& stream)
    {
        char buf[1024];
        auto [ec, n] = co_await read(stream, mutable_buffer(buf, sizeof(buf)));
        if (ec == cond::eof)
        {
            // Handle end-of-file
        }
        else if (ec)
        {
            // Handle other error
        }
        // n bytes were read into buf
    }
    @endcode

    @see ReadStream, MutableBufferSequence
*/
auto
read(
    ReadStream auto& stream,
    MutableBufferSequence auto const& buffers) ->
        task<io_result<std::size_t>>
{
    consuming_buffers consuming(buffers);
    std::size_t const total_size = buffer_size(buffers);
    std::size_t total_read = 0;

    while(total_read < total_size)
    {
        auto [ec, n] = co_await stream.read_some(consuming);
        if(ec)
            co_return {ec, total_read};
        consuming.consume(n);
        total_read += n;
    }

    co_return {{}, total_read};
}

/** Read data from a source into a dynamic buffer.

    This function reads data from the source into the dynamic buffer
    until end-of-file is reached or an error occurs. Data is appended
    to the buffer using prepare/commit semantics.

    The buffer grows using a strategy that starts with `initial_amount`
    bytes and grows by a factor of 1.5 when filled.

    @tparam Source The source type, must satisfy @ref ReadSource.

    @param source The source to read from.
    @param buffers The dynamic buffer to read into.
    @param initial_amount The initial number of bytes to prepare.

    @return A task that yields `(std::error_code, std::size_t)`.
        On success (EOF reached), `ec` is default-constructed and `n`
        is the total number of bytes read. On error, `ec` contains the
        error code and `n` is the total number of bytes read before
        the error.

    @par Example
    @code
    task<void> example(ReadSource auto& source)
    {
        std::string body;
        auto [ec, n] = co_await read(source, string_buffers(body));
        if (ec)
        {
            // Handle error
        }
        // body contains n bytes of data
    }
    @endcode

    @see ReadSource, DynamicBufferParam
*/
auto
read(
    ReadSource auto& source,
    DynamicBufferParam auto&& buffers,
    std::size_t initial_amount = 2048) ->
        task<io_result<std::size_t>>
{
    std::size_t amount = initial_amount;
    std::size_t total_read = 0;
    for(;;)
    {
        auto mb = buffers.prepare(amount);
        auto const mb_size = buffer_size(mb);
        auto [ec, n] = co_await source.read(mb);
        buffers.commit(n);
        total_read += n;
        if(ec == cond::eof)
            co_return {{}, total_read};
        if(ec)
            co_return {ec, total_read};
        if(n == mb_size)
            amount = amount / 2 + amount; // 1.5x growth
    }
}

} // namespace capy
} // namespace boost

#endif
