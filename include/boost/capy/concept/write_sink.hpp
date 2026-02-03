//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_WRITE_SINK_HPP
#define BOOST_CAPY_CONCEPT_WRITE_SINK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/buffer_archetype.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <system_error>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types that provide awaitable write operations to a sink.

    A type satisfies `WriteSink` if it provides `write` and `write_eof`
    member functions that are @ref IoAwaitable and whose return values
    decompose to `(error_code)` or `(error_code,std::size_t)`.

    Use this concept when you need to consume data asynchronously, such
    as writing HTTP response bodies, streaming file contents, or piping
    data through transformations like compression.

    @tparam T The sink type.

    @par Syntactic Requirements

    @li `T` must provide a `write` member function template accepting
        any @ref ConstBufferSequence, returning an awaitable that
        decomposes to `(error_code,std::size_t)`
    @li `T` must provide a `write` member function template accepting
        any @ref ConstBufferSequence and a `bool eof` parameter,
        returning an awaitable that decomposes to `(error_code,std::size_t)`
    @li `T` must provide a `write_eof` member function taking no arguments,
        returning an awaitable that decomposes to `(error_code)`
    @li All return types must satisfy @ref IoAwaitable

    @par Semantic Requirements

    The `write` operation consumes data from the buffer sequence:

    @li On success: `ec` is `false`, and all bytes from the buffer
        sequence have been consumed.
    @li On error: `ec` is `true`.

    The `write` operation with `eof` combines data writing with end-of-stream
    signaling:

    @li If `eof` is `false`, behaves identically to `write(buffers)`.
    @li If `eof` is `true`, writes the data and then finalizes the sink
        as if `write_eof()` were called.
    @li On success: `ec` is `false`, and `n` indicates the number
        of bytes written from the caller's buffer.
    @li On error: `ec` is `true`, and `n` indicates the number of
        bytes written from the caller's buffer before the error occurred.

    The `write_eof` operation signals that no more data will be written:

    @li On success: `ec` is `false`, and the sink is finalized.
    @li On error: `ec` is `true`.

    After `write_eof` returns successfully, or after `write(buffers, true)`
    returns successfully, no further calls to `write` or `write_eof` are
    permitted.

    @par Buffer Lifetime

    The caller must ensure that the memory referenced by the buffer
    sequence remains valid until the `co_await` expression returns.

    @par Conforming Signatures

    @code
    template< ConstBufferSequence Buffers >
    IoAwaitable auto write( Buffers buffers );

    template< ConstBufferSequence Buffers >
    IoAwaitable auto write( Buffers buffers, bool eof );

    IoAwaitable auto write_eof();
    @endcode

    @warning **Coroutine Buffer Lifetime**: When implementing coroutine
    member functions, prefer accepting buffer sequences **by value**
    rather than by reference. Buffer sequences passed by reference may
    become dangling if the caller's stack frame is destroyed before the
    coroutine completes. Passing by value ensures the buffer sequence
    is copied into the coroutine frame and remains valid across
    suspension points.

    @par Example

    @code
    template< WriteSink Sink >
    task<> send_body( Sink& sink, std::string_view data )
    {
        auto [ec, n] = co_await sink.write( make_buffer( data ) );
        if( ec )
            co_return;
        auto [ec2] = co_await sink.write_eof();
    }

    // Or equivalently using the combined overload:
    template< WriteSink Sink >
    task<> send_body2( Sink& sink, std::string_view data )
    {
        auto [ec, n] = co_await sink.write( make_buffer( data ), true );
    }
    @endcode

    @see IoAwaitable, ConstBufferSequence, awaitable_decomposes_to
*/
template<typename T>
concept WriteSink =
    requires(T& sink, const_buffer_archetype buffers, bool eof)
    {
        { sink.write(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(sink.write(buffers)),
            std::error_code, std::size_t>;
        { sink.write(buffers, eof) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(sink.write(buffers, eof)),
            std::error_code, std::size_t>;
        { sink.write_eof() } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(sink.write_eof()),
            std::error_code>;
    };

} // namespace capy
} // namespace boost

#endif
