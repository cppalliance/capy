//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
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
#include <boost/capy/cond.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <boost/capy/concept/io_awaitable.hpp>

#include <cstddef>
#include <span>
#include <system_error>

namespace boost {
namespace capy {

/** Concept for types that produce buffer data asynchronously.

    A type satisfies `BufferSource` if it provides a `pull` member function
    that fills a caller-provided span of buffer descriptors and
    await-returns `(error_code, std::span<const_buffer>)`, plus a `consume` member function
    to indicate how many bytes were used.

    Use this concept when you need to produce data asynchronously for
    transfer to a sink, such as streaming HTTP request bodies or reading
    file contents for transmission.

    @tparam T The source type.

    @par Syntactic Requirements

    @li `T` must provide a `pull` member function accepting a
        `std::span<const_buffer>` for output
    @li The return type must satisfy @ref IoAwaitable
    @li The awaitable must decompose to `(error_code,std::span<const_buffer>)`
        via structured bindings
    @li `T` must provide a `consume` member function accepting a byte count

    @par Semantic Requirements

    The `pull` operation fills the provided buffer span with data starting
    from the current unconsumed position. On return, exactly one of the
    following is true:

    @li **Data available**: `!ec` and `bufs.size() > 0`.
        The returned span contains buffer descriptors.
    @li **Source exhausted**: `ec == cond::eof` and `bufs.empty()`.
        No more data is available; the transfer is complete.
    @li **Error**: `ec` is `true` and `ec != cond::eof`.
        An error occurred.

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
    some_io_awaitable<io_result<std::span<const_buffer>>>
    pull( std::span<const_buffer> dest );

    void consume( std::size_t n ) noexcept;
    @endcode

    @par Example

    @code
    template<BufferSource Source, WriteStream Stream>
    io_task<std::size_t> transfer( Source& source, Stream& stream )
    {
        const_buffer arr[16];
        std::size_t total = 0;
        for(;;)
        {
            auto [ec, bufs] = co_await source.pull( arr );
            if( ec == cond::eof )
                co_return {{}, total};
            if( ec )
                co_return {ec, total};
            auto [write_ec, n] = co_await stream.write_some( bufs );
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
    requires(T& src, std::span<const_buffer> dest, std::size_t n)
    {
        { src.pull(dest) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(src.pull(dest)),
            std::error_code, std::span<const_buffer>>;
        src.consume(n);
    };

} // namespace capy
} // namespace boost

#endif
