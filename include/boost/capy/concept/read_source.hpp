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
#include <system_error>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types that provide awaitable read operations from a source.

    A type satisfies `ReadSource` if it provides a `read` member function
    that accepts any @ref MutableBufferSequence and is an @ref IoAwaitable
    whose return value decomposes to `(error_code, std::size_t)`.

    Use this concept when you need to produce data asynchronously, such
    as reading HTTP request bodies, streaming file contents, or generating
    data through transformations like decompression.

    @tparam T The source type.

    @par Syntactic Requirements

    @li `T` must provide a `read` member function template accepting
        any @ref MutableBufferSequence
    @li The return type must satisfy @ref IoAwaitable
    @li The awaitable must decompose to `(error_code, std::size_t)`
        via structured bindings

    @par Semantic Requirements

    The `read` operation transfers data into the buffer sequence. On
    return, exactly one of the following is true:

    @li **Success**: `ec` is `false` and `n` equals
        `buffer_size( buffers )`. The entire buffer sequence was filled.
    @li **End-of-stream or Error**: `ec` is `true` and `n`
        indicates the number of bytes transferred before the failure.

    If the source reaches end-of-stream before filling the buffer,
    the operation returns with `ec` equal to `true`. Successful
    partial reads are not permitted; either the entire buffer is filled
    or the operation fails with any partial data reported in `n`.

    If `buffer_empty( buffers )` is `true`, the operation completes
    immediately with `ec` equal to `false` and `n` equal to 0.

    When the buffer sequence contains multiple buffers, each buffer is
    filled completely before proceeding to the next.

    @par Buffer Lifetime

    The caller must ensure that the memory referenced by the buffer
    sequence remains valid until the `co_await` expression returns.

    @par Conforming Signatures

    @code
    template<MutableBufferSequence MB>
    some_io_awaitable<io_result<std::size_t>>
    read( MB const& buffers );

    template<MutableBufferSequence MB>
    some_io_awaitable<io_result<std::size_t>>
    read( MB buffers );  // by-value also permitted
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
    template<ReadSource Source>
    task<std::string> read_all( Source& source )
    {
        std::string result;
        char buf[1024];
        for(;;)
        {
            auto [ec, n] = co_await source.read( mutable_buffer( buf ) );
            if( ec == cond::eof )
                break;
            if( ec )
                co_return {};
            result.append( buf, n );
        }
        co_return result;
    }
    @endcode

    @see IoAwaitable, MutableBufferSequence, awaitable_decomposes_to
*/
template<typename T>
concept ReadSource =
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
