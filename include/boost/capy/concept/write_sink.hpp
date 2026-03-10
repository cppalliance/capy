//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
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
#include <boost/capy/concept/write_stream.hpp>
#include <system_error>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types providing complete writes with EOF signaling.

    A type satisfies `WriteSink` if it satisfies @ref WriteStream
    and additionally provides `write`, `write_eof(buffers)`, and
    `write_eof()` member functions that await-return
    `(error_code, std::size_t)`.

    `WriteSink` refines `WriteStream`. Every `WriteSink` is a
    `WriteStream`. Algorithms constrained on `WriteStream` accept
    both raw streams and sinks.

    @tparam T The sink type.

    @par Syntactic Requirements

    @li `T` must satisfy @ref WriteStream (provides `write_some`)
    @li `T` must provide a `write` member function template accepting
        any @ref ConstBufferSequence, returning an awaitable that
        decomposes to `(error_code,std::size_t)`
    @li `T` must provide a `write_eof` member function template
        accepting any @ref ConstBufferSequence, returning an awaitable
        that decomposes to `(error_code,std::size_t)`
    @li `T` must provide a `write_eof` member function taking no
        arguments, returning an awaitable that decomposes to
        `(error_code)`
    @li All return types must satisfy @ref IoAwaitable

    @par Semantic Requirements

    The inherited `write_some` operation attempts to write up to
    `buffer_size( buffers )` bytes (partial write). See @ref WriteStream.

    The `write` operation consumes the entire buffer sequence:

    @li On success: `!ec`, and `n` equals `buffer_size( buffers )`.
    @li On error: `ec`, and `n` indicates the number of bytes
        written before the error.

    The `write_eof(buffers)` operation writes the entire buffer
    sequence and signals end-of-stream atomically:

    @li On success: `!ec`, `n` equals `buffer_size( buffers )`,
        and the sink is finalized.
    @li On error: `ec`, and `n` indicates the number of bytes
        written before the error.

    The `write_eof()` operation signals end-of-stream with no data:

    @li On success: `!ec`, and the sink is finalized.
    @li On error: `ec`.

    After `write_eof` (either overload) returns successfully, no
    further writes or EOF signals are permitted.

    @par Error Reporting
    I/O conditions arising from the underlying I/O system (EOF,
    connection reset, broken pipe, etc.) are reported via the
    `error_code` component of the return value. Failures in the
    library wrapper itself (such as memory allocation failure)
    are reported via exceptions.

    @throws std::bad_alloc If coroutine frame allocation fails.

    @par Buffer Lifetime

    The caller must ensure that the memory referenced by the buffer
    sequence remains valid until the `co_await` expression returns.

    @par Conforming Signatures

    @code
    template< ConstBufferSequence Buffers >
    IoAwaitable auto write_some( Buffers buffers );  // inherited

    template< ConstBufferSequence Buffers >
    IoAwaitable auto write( Buffers buffers );

    template< ConstBufferSequence Buffers >
    IoAwaitable auto write_eof( Buffers buffers );

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
        // Atomic: write all data and signal EOF
        auto [ec, n] = co_await sink.write_eof(
            make_buffer( data ) );
    }

    // Or separately:
    template< WriteSink Sink >
    task<> send_body2( Sink& sink, std::string_view data )
    {
        auto [ec, n] = co_await sink.write(
            make_buffer( data ) );
        if( ec )
            co_return;
        auto [ec2] = co_await sink.write_eof();
    }
    @endcode

    @see WriteStream, IoAwaitable, ConstBufferSequence,
        awaitable_decomposes_to
*/
template<typename T>
concept WriteSink =
    WriteStream<T> &&
    requires(T& sink, const_buffer_archetype buffers)
    {
        { sink.write(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(sink.write(buffers)),
            std::error_code, std::size_t>;
        { sink.write_eof(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(sink.write_eof(buffers)),
            std::error_code, std::size_t>;
        { sink.write_eof() } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(sink.write_eof()),
            std::error_code>;
    };

} // namespace capy
} // namespace boost

#endif
