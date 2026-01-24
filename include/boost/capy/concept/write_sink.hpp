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
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/type_traits.hpp>
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types that provide awaitable write operations to a sink.

    A type satisfies `WriteSink` if it provides `write` and `write_eof`
    member functions that are @ref IoAwaitable and whose return values
    decompose to `( system::error_code )` or `( system::error_code, std::size_t )`.

    Use this concept when you need to consume data asynchronously, such
    as writing HTTP response bodies, streaming file contents, or piping
    data through transformations like compression.

    @tparam T The sink type.

    @par Syntactic Requirements

    @li `T` must provide a `write` member function template accepting
        any @ref ConstBufferSequence, returning an awaitable that
        decomposes to `( system::error_code )`
    @li `T` must provide a `write` member function template accepting
        any @ref ConstBufferSequence and a `bool eof` parameter,
        returning an awaitable that decomposes to
        `( system::error_code, std::size_t )`
    @li `T` must provide a `write_eof` member function taking no arguments,
        returning an awaitable that decomposes to `( system::error_code )`
    @li All return types must satisfy @ref IoAwaitable

    @par Semantic Requirements

    The `write` operation consumes data from the buffer sequence:

    @li On success: `ec.failed()` is `false`, and all bytes from the buffer
        sequence have been consumed.
    @li On error: `ec.failed()` is `true`.

    The `write` operation with `eof` combines data writing with end-of-stream
    signaling:

    @li If `eof` is `false`, behaves identically to `write(buffers)`.
    @li If `eof` is `true`, writes the data and then finalizes the sink
        as if `write_eof()` were called.
    @li On success: `ec.failed()` is `false`, and `n` indicates the number
        of bytes written from the caller's buffer.
    @li On error: `ec.failed()` is `true`, and `n` indicates the number of
        bytes written from the caller's buffer before the error occurred.

    The `write_eof` operation signals that no more data will be written:

    @li On success: `ec.failed()` is `false`, and the sink is finalized.
    @li On error: `ec.failed()` is `true`.

    After `write_eof` returns successfully, or after `write(buffers, true)`
    returns successfully, no further calls to `write` or `write_eof` are
    permitted.

    @par Buffer Lifetime

    The caller must ensure that the memory referenced by the buffer
    sequence remains valid until the `co_await` expression returns.

    @par Conforming Signatures

    @code
    template<ConstBufferSequence CB>
    some_io_awaitable<io_result<>>
    write( CB const& buffers );

    template<ConstBufferSequence CB>
    some_io_awaitable<io_result<std::size_t>>
    write( CB const& buffers, bool eof );

    some_io_awaitable<io_result<>>
    write_eof();
    @endcode

    @par Example

    @code
    template<WriteSink Sink>
    task<void> send_body( Sink& sink, std::string_view data )
    {
        auto [ec] = co_await sink.write( make_buffer( data ) );
        if( ec.failed() )
            co_return;
        auto [ec2] = co_await sink.write_eof();
    }

    // Or equivalently using the combined overload:
    template<WriteSink Sink>
    task<void> send_body2( Sink& sink, std::string_view data )
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
            system::error_code>;
        { sink.write(buffers, eof) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(sink.write(buffers, eof)),
            system::error_code, std::size_t>;
        { sink.write_eof() } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(sink.write_eof()),
            system::error_code>;
    };

} // namespace capy
} // namespace boost

#endif
