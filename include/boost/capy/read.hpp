//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
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
#include <boost/capy/io_task.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/consuming_buffers.hpp>
#include <boost/capy/concept/dynamic_buffer.hpp>
#include <boost/capy/concept/read_source.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <system_error>

#include <cstddef>

namespace boost {
namespace capy {

/** Asynchronously read until the buffer sequence is full.

    Reads data from the stream by calling `read_some` repeatedly
    until the entire buffer sequence is filled or an error occurs.

    @li The operation completes when:
    @li The buffer sequence is completely filled
    @li An error occurs (including `cond::eof`)
    @li The operation is cancelled

    @par Cancellation
    Supports cancellation via `stop_token` propagated through the
    IoAwaitable protocol. When cancelled, returns with `cond::canceled`.

    @param stream The stream to read from. The caller retains ownership.
    @param buffers The buffer sequence to fill. The caller retains
        ownership and must ensure validity until the operation completes.

    @return An awaitable that await-returns `(error_code, std::size_t)`.
        On success, `n` equals `buffer_size(buffers)`. On error,
        `n` is the number of bytes read before the error. Compare
        error codes to conditions:
        @li `cond::eof` - Stream reached end before buffer was filled
        @li `cond::canceled` - Operation was cancelled

    @par Example

    @code
    task<> read_message( ReadStream auto& stream )
    {
        char header[16];
        auto [ec, n] = co_await read( stream, mutable_buffer( header ) );
        if( ec == cond::eof )
            co_return;  // Connection closed
        if( ec )
            detail::throw_system_error( ec );
        // header contains exactly 16 bytes
    }
    @endcode

    @see read_some, ReadStream, MutableBufferSequence
*/
auto
read(
    ReadStream auto& stream,
    MutableBufferSequence auto buffers) ->
        io_task<std::size_t>
{
    consuming_buffers consuming(buffers);
    std::size_t const total_size = buffer_size(buffers);
    std::size_t total_read = 0;

    while(total_read < total_size)
    {
        auto [ec, n] = co_await stream.read_some(consuming);
        consuming.consume(n);
        total_read += n;
        if(ec)
            co_return {ec, total_read};
    }

    co_return {{}, total_read};
}

/** Asynchronously read all data from a stream into a dynamic buffer.

    Reads data by calling `read_some` repeatedly until EOF is reached
    or an error occurs. Data is appended using prepare/commit semantics.
    The buffer grows with 1.5x factor when filled.

    @li The operation completes when:
    @li End-of-stream is reached (`cond::eof`)
    @li An error occurs
    @li The operation is cancelled

    @par Cancellation
    Supports cancellation via `stop_token` propagated through the
    IoAwaitable protocol. When cancelled, returns with `cond::canceled`.

    @param stream The stream to read from. The caller retains ownership.
    @param buffers The dynamic buffer to append data to. Must remain
        valid until the operation completes.
    @param initial_amount Initial bytes to prepare (default 2048).

    @return An awaitable that await-returns `(error_code, std::size_t)`.
        On success (EOF), `ec` is clear and `n` is total bytes read.
        On error, `n` is bytes read before the error. Compare error
        codes to conditions:
        @li `cond::canceled` - Operation was cancelled

    @par Example

    @code
    task<std::string> read_body( ReadStream auto& stream )
    {
        std::string body;
        auto [ec, n] = co_await read( stream, string_dynamic_buffer( &body ) );
        if( ec )
            detail::throw_system_error( ec );
        return body;
    }
    @endcode

    @see read_some, ReadStream, DynamicBufferParam
*/
auto
read(
    ReadStream auto& stream,
    DynamicBufferParam auto&& buffers,
    std::size_t initial_amount = 2048) ->
        io_task<std::size_t>
{
    std::size_t amount = initial_amount;
    std::size_t total_read = 0;
    for(;;)
    {
        auto mb = buffers.prepare(amount);
        auto const mb_size = buffer_size(mb);
        auto [ec, n] = co_await stream.read_some(mb);
        buffers.commit(n);
        total_read += n;
        if(ec == cond::eof)
            co_return {{}, total_read};
        if(ec)
            co_return {ec, total_read};
        if(n == mb_size)
            amount = amount / 2 + amount;
    }
}

/** Asynchronously read all data from a source into a dynamic buffer.

    Reads data by calling `source.read` repeatedly until EOF is reached
    or an error occurs. Data is appended using prepare/commit semantics.
    The buffer grows with 1.5x factor when filled.

    @li The operation completes when:
    @li End-of-stream is reached (`cond::eof`)
    @li An error occurs
    @li The operation is cancelled

    @par Cancellation
    Supports cancellation via `stop_token` propagated through the
    IoAwaitable protocol. When cancelled, returns with `cond::canceled`.

    @param source The source to read from. The caller retains ownership.
    @param buffers The dynamic buffer to append data to. Must remain
        valid until the operation completes.
    @param initial_amount Initial bytes to prepare (default 2048).

    @return An awaitable that await-returns `(error_code, std::size_t)`.
        On success (EOF), `ec` is clear and `n` is total bytes read.
        On error, `n` is bytes read before the error. Compare error
        codes to conditions:
        @li `cond::canceled` - Operation was cancelled

    @par Example

    @code
    task<std::string> read_body( ReadSource auto& source )
    {
        std::string body;
        auto [ec, n] = co_await read( source, string_dynamic_buffer( &body ) );
        if( ec )
            detail::throw_system_error( ec );
        return body;
    }
    @endcode

    @see ReadSource, DynamicBufferParam
*/
auto
read(
    ReadSource auto& source,
    DynamicBufferParam auto&& buffers,
    std::size_t initial_amount = 2048) ->
        io_task<std::size_t>
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
