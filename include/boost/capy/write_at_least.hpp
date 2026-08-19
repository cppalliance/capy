//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_WRITE_AT_LEAST_HPP
#define BOOST_CAPY_WRITE_AT_LEAST_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/consuming_buffers.hpp>
#include <boost/capy/concept/write_stream.hpp>

#include <cstddef>
#include <system_error>

namespace boost {
namespace capy {

/** Write at least a minimum number of bytes to a stream.

    This is a straightforward extension of @ref write. While @ref write
    transfers exactly `buffer_size(buffers)` bytes, `write_at_least`
    transfers at least `n` bytes. The loop stops as soon as `n` bytes
    have been written, even if `buffers` has not been fully consumed.
    Any bytes beyond `n` that a single `stream.write_some` happens to
    transfer are counted. No further awaiting is performed to write the
    remainder.

    Provided for symmetry with @ref read_at_least.

    @par Await-effects

    If `n > buffer_size(buffers)` the request is impossible to satisfy
    and the operation fails immediately with
    `{std::errc::invalid_argument, 0}` without awaiting `stream.write_some`.

    Otherwise writes the contents of `buffers` to `stream` via awaiting
    `stream.write_some` with consecutive portions of data from `buffers`
    until:

    @li either at least `n` bytes have been written,
    @li or a contingency in `stream.write_some` occurs.

    If `n == 0` then no awaiting `stream.write_some` is performed. This is
    not a contingency.

    @par Await-returns
    An object of type `io_result<std::size_t>` destructuring as `[ec, n]`.

    Upon a contingency, the count represents the number of bytes written
    so far.

    Contingencies:

    @li The first contingency reported from awaiting @c stream.write_some
        while fewer than `n` bytes have been written. A contingency that
        accompanies the write which reaches `n` is not reported: a
        satisfied request is a success.

    Notable conditions:

    @li @c std::errc::invalid_argument — `n` exceeds `buffer_size(buffers)`,
    @li @c cond::canceled — Operation was cancelled,
    @li @c std::errc::broken_pipe — Peer closed connection.

    @par Await-postcondition
    On success the returned count is greater than or equal to `n` and
    less than or equal to `buffer_size(buffers)`, and `ec` is success.
    Otherwise `ec` is set.

    @param stream The stream to write to. If the lifetime of `stream` ends
    before the coroutine finishes, the behavior is undefined.

    @param buffers The buffer sequence to write. If the lifetime of the
    buffer sequence represented by `buffers` ends before the coroutine
    finishes, the behavior is undefined.

    @param n The minimum number of bytes to write. Must not exceed
    `buffer_size(buffers)`.

    @return A task yielding `io_result<std::size_t>` whose second element
    is the number of bytes written.

    @par Remarks
    Supports _IoAwaitable cancellation_.

    @par Example

    @code
    capy::task<> flush_at_least(capy::WriteStream auto& stream, std::string_view data)
    {
        auto [ec, n] = co_await capy::write_at_least(
            stream, capy::make_buffer(data), 8);
        if(ec)
            throw std::system_error(ec);

        // at least 8 bytes written; n may be larger
    }
    @endcode

    @see write, WriteStream, ConstBufferSequence
*/
template <WriteStream S, ConstBufferSequence CB>
auto
write_at_least(S& stream, CB buffers, std::size_t n) -> io_task<std::size_t>
{
    consuming_buffers consuming(buffers);
    std::size_t const total_size = buffer_size(buffers);

    if(n > total_size)
        co_return {make_error_code(std::errc::invalid_argument), 0};

    std::size_t total_written = 0;

    while(total_written < n)
    {
        auto [ec, m] = co_await stream.write_some(consuming.data());
        consuming.consume(m);
        total_written += m;
        // A contingency that still satisfied the request is a success:
        // report it only when fewer than n bytes were written.
        if(ec && total_written < n)
            co_return {ec, total_written};
    }

    co_return {std::error_code(), total_written};
}

} // namespace capy
} // namespace boost

#endif
