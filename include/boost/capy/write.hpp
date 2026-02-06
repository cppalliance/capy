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
#include <boost/capy/io_task.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/consuming_buffers.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <system_error>

#include <cstddef>

namespace boost {
namespace capy {

/** Asynchronously write the entire buffer sequence.

    Writes data to the stream by calling `write_some` repeatedly
    until the entire buffer sequence is written or an error occurs.

    @li The operation completes when:
    @li The entire buffer sequence has been written
    @li An error occurs
    @li The operation is cancelled

    @par Cancellation
    Supports cancellation via `stop_token` propagated through the
    IoAwaitable protocol. When cancelled, returns with `cond::canceled`.

    @param stream The stream to write to. The caller retains ownership.
    @param buffers The buffer sequence to write. The caller retains
        ownership and must ensure validity until the operation completes.

    @return An awaitable yielding `(error_code, std::size_t)`.
        On success, `n` equals `buffer_size(buffers)`. On error,
        `n` is the number of bytes written before the error. Compare
        error codes to conditions:
        @li `cond::canceled` - Operation was cancelled
        @li `std::errc::broken_pipe` - Peer closed connection

    @par Example

    @code
    task<> send_response( WriteStream auto& stream, std::string_view body )
    {
        auto [ec, n] = co_await write( stream, make_buffer( body ) );
        if( ec )
            detail::throw_system_error( ec );
        // All bytes written successfully
    }
    @endcode

    @see write_some, WriteStream, ConstBufferSequence
*/
auto
write(
    WriteStream auto& stream,
    ConstBufferSequence auto const& buffers) ->
        io_task<std::size_t>
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

} // namespace capy
} // namespace boost

#endif
