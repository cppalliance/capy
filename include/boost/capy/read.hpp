//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
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
#include <boost/capy/concept/read_stream.hpp>

#include <algorithm>
#include <cstddef>

namespace boost {
namespace capy {

/** Read data from a stream until the buffer sequence is full.

    @par Await-effects

    Reads data from `stream` via awaiting `stream.read_some` repeatedly
    until:

    @li either the entire buffer sequence  @c buffers is filled,
    @li or a contingency occurs on `stream.read_some`.

    If `buffer_size(buffers) == 0` then no awaiting `stream.read_some`
    is performed. This is not a contingency.

    @par Await-returns
    An object of type `io_result<std::size_t>` destructuring as `[ec, n]`.

    Upon a contingency, `n` represents the number of bytes read so far,
    inclusive of the last partial read.

    Contingencies:

    @li The first contingency reported from awaiting @c stream.read_some
        while `buffers` is not yet filled. A contingency that accompanies
        the read which fills `buffers` is not reported: a completed
        transfer is a success.

    Notable conditions:

    @li @c cond::canceled — Operation was cancelled,
    @li @c cond::eof — Stream reached end before @c buffers was filled.

    @par Await-postcondition
    If `n == buffer_size(buffers)` the transfer completed and `ec` is
    success; otherwise `ec` is set.

    @param stream The stream to read from. If the lifetime of `stream` ends
    before the coroutine finishes, the behavior is undefined.

    @param buffers The buffer sequence to fill. If the lifetime of the buffer
    sequence represented by `buffers` ends before the coroutine finishes, the behavior is undefined.

    @return A task yielding `io_result<std::size_t>` whose second element
    is the number of bytes read.

    @par Remarks
    Supports _IoAwaitable cancellation_.


    @par Example

    @code
    capy::task<> process_message(capy::ReadStream auto& stream)
    {
        std::vector<char> header(16);  // known header size for some protocol
        auto [ec, n] = co_await capy::read(stream, capy::make_buffer(header));
        if (ec == capy::cond::eof)
            co_return;  // Connection closed
        if (ec)
            throw std::system_error(ec);

        // at this point `header` contains exactly 16 bytes
    }
    @endcode

    @see ReadStream, MutableBufferSequence
*/
template <typename S, typename MB>
  requires ReadStream<S> && MutableBufferSequence<MB>
auto
read(S& stream, MB buffers) ->
        io_task<std::size_t>
{
    consuming_buffers consuming(buffers);
    std::size_t const total_size = buffer_size(buffers);
    std::size_t total_read = 0;

    while(total_read < total_size)
    {
        auto [ec, n] = co_await stream.read_some(consuming.data());
        consuming.consume(n);
        total_read += n;
        // A contingency that still completed the transfer is a success:
        // report it only when the buffer was not filled.
        if(ec && total_read < total_size)
            co_return {ec, total_read};
    }

    co_return {std::error_code(), total_read};
}

} // namespace capy
} // namespace boost

#endif
