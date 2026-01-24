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
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/type_traits.hpp>
#include <boost/system/error_code.hpp>

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

    The `read` operation fills data into the buffer sequence:

    @li On success: `!ec.failed()` is `true`, and `n` is the number of bytes
        read (at least 1).
    @li On error: `ec.failed()` is `true`, and `n` is 0.
    @li On end-of-file: `ec == cond::eof` is `true`, and `n` is 0.
        This is typically satisfied by returning `error::eof`.

    If `buffer_size( buffers ) == 0`, the operation completes
    immediately. `!ec.failed()` is `true`, and `n` is 0.

    Buffers in the sequence are filled completely before proceeding
    to the next buffer.

    @par Buffer Lifetime

    The caller must ensure that the memory referenced by the buffer
    sequence remains valid until the `co_await` expression returns.

    @par Conforming Signatures

    @code
    template<MutableBufferSequence MB>
    some_io_awaitable<io_result<std::size_t>>
    read( MB const& buffers );
    @endcode

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
            if( ec.failed() )
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
            system::error_code, std::size_t>;
    };

} // namespace capy
} // namespace boost

#endif
