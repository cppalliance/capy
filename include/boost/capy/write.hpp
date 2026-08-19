//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
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

/** Write an entire buffer sequence to a stream.

    @par Await-effects

    Writes the contents of `buffers` to `stream` via awaiting
    `stream.write_some` with consecutive portions of data from `buffers`
    until:

    @li either the full content of @c buffers is processed,
    @li or a contingency in `stream.write_some` occurs.

    If `buffer_size(buffers) == 0` then no awaiting `stream.write_some`
    is performed. This is not a contingency.


    @par Await-returns

    An object of type `io_result<std::size_t>` destructuring as `[ec, n]`.

    Upon a contingency, `n` represents the number of bytes written
    so far.

    Otherwise `n` represents the number of bytes written.

    Contingencies:

    @li The first contingency reported from awaiting @c stream.write_some
    while not all bytes have been written. A contingency that accompanies
    the write which transfers the last bytes is not reported: a completed
    transfer is a success.

    Notable conditions:

    @li @c cond::canceled — Operation was cancelled,
    @li @c std::errc::broken_pipe — Peer closed connection.


    @par Await-postcondition

    If `n == buffer_size(buffers)` the transfer completed and `ec` is
    success; otherwise `ec` is set.


    @param stream The stream to write to. If the lifetime of `stream` ends
    before the coroutine finishes, the behavior is undefined.

    @param buffers The buffer sequence to write. If the lifetime of the buffer
    sequence represented by `buffers` ends
    before the coroutine finishes, the behavior is undefined.

    @return A task yielding `io_result<std::size_t>` whose second element
    is the number of bytes written.

    @par Remarks

    Supports _IoAwaitable cancellation_.

    @par Example

    @code
    capy::task<> send_response(capy::WriteStream auto& stream, std::string_view body)
    {
        auto [ec, n] = co_await capy::write(stream, capy::make_buffer(body));
        if (ec)
            throw std::system_error(ec);

        // All bytes written successfully
    }
    @endcode

    @see WriteStream, ConstBufferSequence, IoAwaitable, io_result, cond.
*/
template <WriteStream S, ConstBufferSequence CB>
auto write(S& stream, CB buffers) -> io_task<std::size_t>
{
    consuming_buffers consuming(buffers);
    std::size_t const total_size = buffer_size(buffers);
    std::size_t total_written = 0;

    while(total_written < total_size)
    {
        auto [ec, n] = co_await stream.write_some(consuming.data());
        consuming.consume(n);
        total_written += n;
        // A contingency that still completed the transfer is a success:
        // report it only when not all bytes were written.
        if(ec && total_written < total_size)
            co_return {ec, total_written};
    }

    co_return {std::error_code(), total_written};
}

} // namespace capy
} // namespace boost

#endif
