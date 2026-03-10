//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_READ_STREAM_HPP
#define BOOST_CAPY_CONCEPT_READ_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/buffer_archetype.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <system_error>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types providing awaitable read operations.

    A type satisfies `ReadStream` if it provides a `read_some`
    member function template that accepts any @ref MutableBufferSequence
    and await-returns `(error_code, std::size_t)`.

    @par Syntactic Requirements
    @li `T` must provide a `read_some` member function template
        accepting any @ref MutableBufferSequence
    @li The return type of `read_some` must satisfy @ref IoAwaitable
    @li The awaitable's result must decompose to
        `(error_code,std::size_t)` via structured bindings

    @par Semantic Requirements
    Attempts to read up to `buffer_size( buffers )` bytes from
    the stream into the buffer sequence.

    If `buffer_size( buffers ) > 0`:

    @li If `!ec`, then `n >= 1 && n <= buffer_size( buffers )`.
        `n` bytes were read into the buffer sequence.
    @li If `ec`, then `n >= 0 && n <= buffer_size( buffers )`.
        `n` is the number of bytes read before the I/O
        condition arose.

    If `buffer_empty( buffers )` is `true`, `n` is 0. The empty
    buffer is not itself a cause for error, but `ec` may reflect
    the state of the stream.

    Buffers in the sequence are filled in order.

    @par Error Reporting
    I/O conditions arising from the underlying I/O system (EOF,
    connection reset, broken pipe, etc.) are reported via the
    `error_code` component of the return value. Failures in the
    library wrapper itself (such as memory allocation failure)
    are reported via exceptions.

    @throws std::bad_alloc If coroutine frame allocation fails.

    @par Buffer Lifetime
    The caller must ensure that the memory referenced by `buffers`
    remains valid until the `co_await` expression returns.

    @par Conforming Signatures
    @code
    // Templated for any MutableBufferSequence
    template< MutableBufferSequence MB >
    IoAwaitable auto read_some( MB const& buffers );

    template< MutableBufferSequence MB >
    IoAwaitable auto read_some( MB buffers );  // by-value also permitted
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
    template< ReadStream Stream >
    task<> read_all( Stream& s, char* buf, std::size_t size )
    {
        std::size_t total = 0;
        while( total < size )
        {
            auto [ec, n] = co_await s.read_some(
                mutable_buffer( buf + total, size - total ) );
            total += n;
            if( ec )
                co_return;
        }
    }
    @endcode

    @see IoAwaitable, MutableBufferSequence, awaitable_decomposes_to
*/
template<typename T>
concept ReadStream =
    requires(T& stream, mutable_buffer_archetype buffers)
    {
        { stream.read_some(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(stream.read_some(buffers)),
            std::error_code, std::size_t>;
    };

} // namespace capy
} // namespace boost

#endif
