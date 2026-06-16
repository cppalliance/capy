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
#include <boost/capy/buffers/buffer_slice.hpp>
#include <boost/capy/concept/dynamic_buffer.hpp>
#include <boost/capy/concept/read_source.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <system_error>

#include <cstddef>

namespace boost {
namespace capy {

/** Read data from a stream until the buffer sequence is full.

    @par Await-effects

    Reads data from `stream` via awaiting `stream.read_some` repeatedly
    until:

    @li either the entire buffer sequence  @c buffers is filled,
    @li or a contingency occurs.

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
    @li @c cond::eof — Stream reached end before `buffers` was filled.

    @par Await-postcondition
    If `n == buffer_size(buffers)` the transfer completed and `ec` is
    success; otherwise `ec` is set.

    @param stream The stream to read from. If the lifetime of `stream` ends
    before the coroutine finishes, the behavior is undefined.

    @param buffers The buffer sequence to fill. If the lifetime of the buffer
    sequence represented by `buffers` ends before the coroutine finishes, the behavior is undefined.


    @par Remarks
    Supports _IoAwaitable cancellation_.


    @par Example

    @code
    capy::task<> process_message(capy::ReadStream auto& stream)
    {
        std::vector<char> header(16);  // known header size for some protocol
        auto [ec, n] = co_await capy::read(stream, capy::mutable_buffer(header));
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
    auto consuming = buffer_slice(buffers);
    std::size_t const total_size = buffer_size(buffers);
    std::size_t total_read = 0;

    while(total_read < total_size)
    {
        auto [ec, n] = co_await stream.read_some(consuming.data());
        consuming.remove_prefix(n);
        total_read += n;
        // A contingency that still completed the transfer is a success:
        // report it only when the buffer was not filled.
        if(ec && total_read < total_size)
            co_return {ec, total_read};
    }

    co_return {{}, total_read};
}

/** Read all data from a stream into a dynamic buffer.

    @par Await-effects

    Reads data from `stream` via awaiting `stream.read_some` repeatedly
    and appending the results to `dynbuf`,
    until a contingency occurs.

    Data is appended using prepare/commit semantics.
    The buffer grows with 1.5x factor when filled.

    @par Await-returns

    An object of type `io_result<std::size_t>` destructuring as `[ec, n]`.

    `n` represents the total number of bytes read,
    inclusive of the last partial read.

    Contingencies:

    @li The first contingency, other than one matching to @c cond::eof, reported from awaiting @c stream.read_some .

    @par Await-throws
    `std::bad_alloc` when append to `dynbuf` fails.

    @param stream The stream to read from. If the lifetime of `stream` ends
    before the coroutine finishes, the behavior is undefined.

    @param dynbuf The dynamic buffer to append data to. If the lifetime of the buffer
    sequence represented by `dynbuf` ends before the coroutine finishes, the behavior is undefined.

    @param initial_amount Initial bytes to prepare (default 2048).

    
    @par Remarks
    Supports _IoAwaitable cancellation_.

    @par Example

    @code
    capy::task<std::string> read_body(capy::ReadStream auto& stream)
    {
        std::string body;
        auto [ec, n] = co_await capy::read(stream, capy::dynamic_buffer(body));
        if (ec)
            throw std::system_error(ec);
        return body;
    }
    @endcode

    @see read_some, ReadStream, DynamicBufferParam
*/
template <typename S, typename DB>
  requires ReadStream<S> && DynamicBufferParam<DB>
auto
read(
    S& stream,
    DB&& dynbuf,
    std::size_t initial_amount = 2048) ->
        io_task<std::size_t>
{
    std::size_t amount = initial_amount;
    std::size_t total_read = 0;
    for(;;)
    {
        auto mb = dynbuf.prepare(amount);
        auto const mb_size = buffer_size(mb);
        auto [ec, n] = co_await stream.read_some(mb);
        dynbuf.commit(n);
        total_read += n;
        if(ec == cond::eof)
            co_return {{}, total_read};
        if(ec)
            co_return {ec, total_read};
        if(n == mb_size)
            amount = amount / 2 + amount;
    }
}

/** Read all data from a source into a dynamic buffer.

    @par Await-effects

    Reads data from `stream` by calling `source.read` repeatedly 
    and appending it to `dynbuf` until a contingency occurs.
    The last, potenitally partial, read is also appended.
    
    Data is appended using prepare/commit semantics.
    The buffer grows with 1.5x factor when filled.

    @par Await-returns

    An object of type `io_result<std::size_t>` destructuring as `[ec, n]`.

    `n` represents the total number of bytes read,
    inclusive of the last partial read.


    Contingencies:

    @li The first contingency, other than one matching to @c cond::eof, reported from awaiting @c stream.read_some .

    @par Await-throws

    `std::bad_alloc` when append to `dynbuf` fails.

    @param source The source to read from. If the lifetime of `source` ends
    before the coroutine finishes, the behavior is undefined.

    @param dynbuf The dynamic buffer to append data to. If the lifetime of the 
    buffer sequence represented by `dynbuf` ends before the coroutine finishes, 
    the behavior is undefined.

    @param initial_amount Initial bytes to prepare (default 2048).

    @par Remarks
    Supports _IoAwaitable cancellation_.

    @par Example

    @code
    capy::task<std::string> read_body(capy::ReadSource auto& source)
    {
        std::string body;
        auto [ec, n] = co_await capy::read(source, capy::dynamic_buffer(body));
        if (ec)
            throw std::system_error(ec);
        return body;
    }
    @endcode

    @see ReadSource, DynamicBufferParam
*/
template <typename S, typename DB>
  requires ReadSource<S> && DynamicBufferParam<DB>
auto
read(
    S& source,
    DB&& dynbuf,
    std::size_t initial_amount = 2048) ->
        io_task<std::size_t>
{
    std::size_t amount = initial_amount;
    std::size_t total_read = 0;
    for(;;)
    {
        auto mb = dynbuf.prepare(amount);
        auto const mb_size = buffer_size(mb);
        auto [ec, n] = co_await source.read(mb);
        dynbuf.commit(n);
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
