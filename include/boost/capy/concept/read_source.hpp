//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_READ_SOURCE_HPP
#define BOOST_CAPY_CONCEPT_READ_SOURCE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/buffer_archetype.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <system_error>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types providing complete reads from a data source.

    A type satisfies `ReadSource` if it satisfies @ref ReadStream
    and additionally provides a `read` member function that accepts
    any @ref MutableBufferSequence and is an @ref IoAwaitable whose
    return value decomposes to `(error_code, std::size_t)`.

    `ReadSource` refines `ReadStream`. Every `ReadSource` is a
    `ReadStream`. Algorithms constrained on `ReadStream` accept both
    raw streams and sources.

    @tparam T The source type.

    @par Syntactic Requirements

    @li `T` must satisfy @ref ReadStream (provides `read_some`)
    @li `T` must provide a `read` member function template accepting
        any @ref MutableBufferSequence
    @li The return type of `read` must satisfy @ref IoAwaitable
    @li The awaitable must decompose to `(error_code, std::size_t)`
        via structured bindings

    @par Semantic Requirements

    The inherited `read_some` operation reads one or more bytes
    (partial read). See @ref ReadStream.

    The `read` operation fills the entire buffer sequence. On return,
    exactly one of the following is true:

    @li **Success**: `!ec` and `n` equals `buffer_size( buffers )`.
        The entire buffer sequence was filled.
    @li **End-of-stream**: `ec == cond::eof` and `n` indicates the
        number of bytes transferred before EOF was reached.
    @li **Error**: `ec` and `n` indicates the number of bytes
        transferred before the error.

    Successful partial reads are not permitted; either the entire
    buffer is filled or the operation returns with an error.

    If `buffer_empty( buffers )` is `true`, the operation completes
    immediately with `!ec` and `n` equal to 0.

    When the buffer sequence contains multiple buffers, each buffer is
    filled completely before proceeding to the next.

    @par Buffer Lifetime

    The caller must ensure that the memory referenced by the buffer
    sequence remains valid until the `co_await` expression returns.

    @par Conforming Signatures

    @code
    template< MutableBufferSequence MB >
    IoAwaitable auto read_some( MB buffers );   // inherited from ReadStream

    template< MutableBufferSequence MB >
    IoAwaitable auto read( MB buffers );
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
    template< ReadSource Source >
    task<> read_header( Source& source )
    {
        char header[16];
        auto [ec, n] = co_await source.read(
            mutable_buffer( header ) );
        if( ec )
            co_return;
        // header contains exactly 16 bytes
    }
    @endcode

    @see ReadStream, IoAwaitable, MutableBufferSequence,
        awaitable_decomposes_to
*/
template<typename T>
concept ReadSource =
    ReadStream<T> &&
    requires(T& source, mutable_buffer_archetype buffers)
    {
        { source.read(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(source.read(buffers)),
            std::error_code, std::size_t>;
    };

} // namespace capy
} // namespace boost

#endif
