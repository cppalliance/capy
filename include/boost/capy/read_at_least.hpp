//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_READ_AT_LEAST_HPP
#define BOOST_CAPY_READ_AT_LEAST_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/consuming_buffers.hpp>
#include <boost/capy/concept/read_stream.hpp>

#include <cstddef>
#include <system_error>

namespace boost {
namespace capy {

/** Read at least a minimum number of bytes from a stream.

    This is a straightforward extension of @ref read. While @ref read
    transfers exactly `buffer_size(buffers)` bytes, `read_at_least`
    transfers at least `n` bytes: the loop stops as soon as `n` bytes
    have been read, even if `buffers` is not yet full. Any bytes beyond
    `n` that a single `stream.read_some` happens to deliver (up to the
    capacity of `buffers`) are kept, but no further awaiting is performed
    to fill the remainder.

    This is useful when a caller has a required amount of data `n` that
    must be met or exceeded, while the subsequent capacity of `buffers`
    is optional and should not block.

    @par Await-effects

    If `n > buffer_size(buffers)` the request is impossible to satisfy
    and the operation fails immediately with
    `{std::errc::invalid_argument, 0}` without awaiting `stream.read_some`.

    Otherwise reads data from `stream` via awaiting `stream.read_some`
    repeatedly until:

    @li either at least `n` bytes have been read,
    @li or a contingency occurs on `stream.read_some`.

    If `n == 0` then no awaiting `stream.read_some` is performed. This is
    not a contingency.

    @par Await-returns
    An object of type `io_result<std::size_t>` destructuring as `[ec, n]`.

    Upon a contingency, the count represents the number of bytes read so
    far, inclusive of the last partial read.

    Contingencies:

    @li The first contingency reported from awaiting @c stream.read_some
        while fewer than `n` bytes have been read. A contingency that
        accompanies the read which reaches `n` is not reported: a
        satisfied request is a success.

    Notable conditions:

    @li @c std::errc::invalid_argument — `n` exceeds `buffer_size(buffers)`,
    @li @c cond::canceled — Operation was cancelled,
    @li @c cond::eof — Stream reached end before `n` bytes were read.

    @par Await-postcondition
    On success the returned count is greater than or equal to `n` and
    less than or equal to `buffer_size(buffers)`, and `ec` is success;
    otherwise `ec` is set.

    @param stream The stream to read from. If the lifetime of `stream` ends
    before the coroutine finishes, the behavior is undefined.

    @param buffers The buffer sequence to read into. If the lifetime of the
    buffer sequence represented by `buffers` ends before the coroutine
    finishes, the behavior is undefined.

    @param n The minimum number of bytes to read. Must not exceed
    `buffer_size(buffers)`.

    @par Remarks
    Supports _IoAwaitable cancellation_.

    @par Example

    @code
    capy::task<> fill_buffer(capy::ReadStream auto& stream)
    {
        std::vector<char> storage(4096);  // generous capacity
        // Require 16 header bytes; opportunistically take more.
        auto [ec, n] = co_await capy::read_at_least(
            stream, capy::make_buffer(storage), 16);
        if(ec)
            throw std::system_error(ec);

        // at least 16 bytes are available; n may be larger
    }
    @endcode

    @see read, ReadStream, MutableBufferSequence
*/
template <typename S, typename MB>
  requires ReadStream<S> && MutableBufferSequence<MB>
auto
read_at_least(S& stream, MB buffers, std::size_t n) ->
        io_task<std::size_t>
{
    consuming_buffers consuming(buffers);
    std::size_t const total_size = buffer_size(buffers);

    if(n > total_size)
        co_return {make_error_code(std::errc::invalid_argument), 0};

    std::size_t total_read = 0;

    while(total_read < n)
    {
        auto [ec, m] = co_await stream.read_some(consuming.data());
        consuming.consume(m);
        total_read += m;
        // A contingency that still satisfied the request is a success:
        // report it only when fewer than n bytes were read.
        if(ec && total_read < n)
            co_return {ec, total_read};
    }

    co_return {std::error_code(), total_read};
}

} // namespace capy
} // namespace boost

#endif
