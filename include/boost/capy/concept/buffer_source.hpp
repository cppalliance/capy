//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_BUFFER_SOURCE_HPP
#define BOOST_CAPY_CONCEPT_BUFFER_SOURCE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <system_error>

#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types that produce buffer data asynchronously.

    A type satisfies `BufferSource` if it provides a `pull` member function
    that fills a caller-provided array of buffer descriptors and is an
    @ref IoAwaitable whose return value decomposes to `(error_code,std::size_t)`,
    plus a `consume` member function to indicate how many bytes were used.

    Use this concept when you need to produce data asynchronously for
    transfer to a sink, such as streaming HTTP request bodies or reading
    file contents for transmission.

    @tparam T The source type.

    @par Syntactic Requirements

    @li `T` must provide a `pull` member function accepting a pointer to
        `const_buffer` and a maximum count
    @li The return type must satisfy @ref IoAwaitable
    @li The awaitable must decompose to `(error_code,std::size_t)`
        via structured bindings
    @li `T` must provide a `consume` member function accepting a byte count

    @par Semantic Requirements

    The `pull` operation fills the provided buffer array with data starting
    from the current unconsumed position. On return, exactly one of the
    following is true:

    @li **Data available**: `ec` is `false` and `count > 0`.
        The array contains `count` buffer descriptors.
    @li **Source exhausted**: `ec` is `false` and `count == 0`.
        No more data is available; the transfer is complete.
    @li **Error**: `ec` is `true`. An error occurred.

    Calling `pull` multiple times without intervening `consume` returns
    the same unconsumed data. The `consume` operation advances the read
    position by the specified number of bytes. The next `pull` returns
    data starting after the consumed bytes.

    @par Buffer Lifetime

    The memory referenced by the returned buffer descriptors must remain
    valid until the next call to `pull`, `consume`, or until the source
    is destroyed.

    @par Conforming Signatures

    @code
    some_io_awaitable<io_result<std::size_t>>
    pull( const_buffer* arr, std::size_t max_count );

    void consume( std::size_t n ) noexcept;
    @endcode

    @par Example

    @code
    template<BufferSource Source, WriteStream Stream>
    task<io_result<std::size_t>> transfer( Source& source, Stream& stream )
    {
        const_buffer arr[16];
        std::size_t total = 0;
        for(;;)
        {
            auto [ec, count] = co_await source.pull( arr, 16 );
            if( ec )
                co_return {ec, total};
            if( count == 0 )
                co_return {{}, total};
            auto [write_ec, n] = co_await stream.write_some(
                std::span( arr, count ) );
            if( write_ec )
                co_return {write_ec, total};
            source.consume( n );
            total += n;
        }
    }
    @endcode

    @see IoAwaitable, WriteSink, awaitable_decomposes_to
*/
template<typename T>
concept BufferSource =
    requires(T& src, const_buffer* arr, std::size_t max_count, std::size_t n)
    {
        { src.pull(arr, max_count) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(src.pull(arr, max_count)),
            std::error_code, std::size_t>;
        src.consume(n);
    };

} // namespace capy
} // namespace boost

#endif
