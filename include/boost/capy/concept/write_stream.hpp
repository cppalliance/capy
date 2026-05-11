//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_WRITE_STREAM_HPP
#define BOOST_CAPY_CONCEPT_WRITE_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/buffer_archetype.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <system_error>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types that provide awaitable write operations.

    A type satisfies `WriteStream` if it provides a `write_some`
    member function template that accepts any @ref ConstBufferSequence
    and await-returns `(error_code, std::size_t)`.

    @tparam T The stream type.

    @par Syntactic Requirements

    @li `T` must provide a `write_some` member function template
        accepting any @ref ConstBufferSequence
    @li The return type of `write_some` must satisfy @ref IoAwaitable
    @li The awaitable's result must decompose to
        `(error_code,std::size_t)` via structured bindings

    @par Semantic Requirements

    Attempts to write up to `buffer_size( buffers )` bytes from
    the buffer sequence to the stream.

    If `buffer_size( buffers ) > 0`:

    @li If `!ec`, then `n >= 1 && n <= buffer_size( buffers )`.
        `n` bytes were written from the buffer sequence.
    @li If `ec`, then `n >= 0 && n <= buffer_size( buffers )`.
        `n` is the number of bytes written before the I/O
        condition arose.

    If `buffer_empty( buffers )` is `true`, `n` is 0. The empty
    buffer is not itself a cause for error, but `ec` may reflect
    the state of the stream.

    Buffers in the sequence are consumed in order.

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
    template< ConstBufferSequence Buffers >
    IoAwaitable auto write_some( Buffers buffers );
    @endcode

    @warning **Pass buffer sequences by value.** A by-value parameter
    is copied into the coroutine frame (or the awaitable's state),
    so the returned awaitable is self-contained and may be stored,
    moved across threads, or wrapped into a sender without lifetime
    concerns. A by-const-reference parameter binds to caller storage
    and is only safe when the awaitable is consumed immediately by
    `co_await` in the same scope; storing such an awaitable produces
    a dangling reference.

    @note Callers who want to avoid copying an expensive buffer
    sequence (for example, a `std::vector<const_buffer>` with many
    entries) can pass `std::views::all(seq)` at the call site. The
    resulting `ref_view` satisfies the buffer-sequence concepts and
    copies in O(1). See `doc/buffers-passing-rationale.md`.

    @par Example

    @code
    template< WriteStream Stream >
    task<> write_all( Stream& s, char const* buf, std::size_t size )
    {
        std::size_t total = 0;
        while( total < size )
        {
            auto [ec, n] = co_await s.write_some(
                const_buffer( buf + total, size - total ) );
            total += n;
            if( ec )
                co_return;
        }
    }
    @endcode

    @see IoAwaitable, ConstBufferSequence, awaitable_decomposes_to
*/
template<typename T>
concept WriteStream =
    requires(T& stream, const_buffer_archetype buffers)
    {
        { stream.write_some(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(stream.write_some(buffers)),
            std::error_code, std::size_t>;
    };

} // namespace capy
} // namespace boost

#endif
