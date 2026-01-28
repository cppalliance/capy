//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_BUFFER_SINK_HPP
#define BOOST_CAPY_CONCEPT_BUFFER_SINK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <system_error>

#include <concepts>
#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types that consume buffer data using callee-owned buffers.

    A type satisfies `BufferSink` if it provides a synchronous `prepare`
    member function that fills a caller-provided array with mutable buffer
    descriptors pointing to the sink's internal storage, and asynchronous
    `commit` and `commit_eof` member functions to finalize written data.

    This concept models the "callee owns buffers" pattern where the sink
    provides writable memory and the caller writes directly into it,
    enabling zero-copy data transfer. Compare with @ref WriteSink which
    uses the "caller owns buffers" pattern.

    @tparam T The sink type.

    @par Syntactic Requirements

    @li `T` must provide a synchronous `prepare` member function accepting
        a pointer to `mutable_buffer` and a maximum count, returning
        `std::size_t` (the number of buffers filled)
    @li `T` must provide `commit(n)` returning an @ref IoAwaitable that
        decomposes to `(error_code)`
    @li `T` must provide `commit(n, eof)` returning an @ref IoAwaitable
        that decomposes to `(error_code)`
    @li `T` must provide `commit_eof()` returning an @ref IoAwaitable
        that decomposes to `(error_code)`

    @par Semantic Requirements

    The `prepare` operation provides writable buffer space:

    @li Returns `count` indicating how many buffer descriptors were filled
    @li The returned buffers point to the sink's internal storage
    @li If `count == 0`, the sink has no available space; caller should
        call `commit` to flush data and try again

    The `commit` operation finalizes written data:

    @li Commits `n` bytes written to the most recent `prepare` buffers
    @li May trigger underlying I/O (flush to socket, compression, etc.)
    @li On success: `ec` is `false`
    @li On error: `ec` is `true`

    The `commit` operation with `eof` combines data commit with end-of-stream:

    @li If `eof` is `false`, behaves identically to `commit(n)`
    @li If `eof` is `true`, commits data and finalizes the sink
    @li After success with `eof == true`, no further operations are permitted

    The `commit_eof` operation signals end-of-stream with no data:

    @li Equivalent to `commit(0, true)`
    @li On success: `ec` is `false`, sink is finalized
    @li On error: `ec` is `true`

    @par Buffer Lifetime

    Buffers returned by `prepare` remain valid until the next call to
    `prepare`, `commit`, `commit_eof`, or until the sink is destroyed.

    @par Conforming Signatures

    @code
    std::size_t prepare( mutable_buffer* arr, std::size_t max_count );

    IoAwaitable auto commit( std::size_t n );
    IoAwaitable auto commit( std::size_t n, bool eof );
    IoAwaitable auto commit_eof();
    @endcode

    @par Example

    @code
    template<BufferSource Source, BufferSink Sink>
    task<io_result<std::size_t>> transfer( Source& source, Sink& sink )
    {
        const_buffer src_arr[16];
        mutable_buffer dst_arr[16];
        std::size_t total = 0;

        for(;;)
        {
            auto [ec1, src_count] = co_await source.pull( src_arr, 16 );
            if( ec1 )
                co_return {ec1, total};

            if( src_count == 0 )
            {
                auto [eof_ec] = co_await sink.commit_eof();
                co_return {eof_ec, total};
            }

            std::size_t dst_count = sink.prepare( dst_arr, 16 );
            std::size_t n = buffer_copy(
                std::span( dst_arr, dst_count ),
                std::span( src_arr, src_count ) );

            auto [ec2] = co_await sink.commit( n );
            if( ec2 )
                co_return {ec2, total};

            total += n;
        }
    }
    @endcode

    @see BufferSource, WriteSink, IoAwaitable, awaitable_decomposes_to
*/
template<typename T>
concept BufferSink =
    requires(T& sink, mutable_buffer* arr, std::size_t max_count, std::size_t n, bool eof)
    {
        // Synchronous: get writable buffers from sink's internal storage
        { sink.prepare(arr, max_count) } -> std::convertible_to<std::size_t>;

        // Async: commit n bytes written
        { sink.commit(n) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(sink.commit(n)),
            std::error_code>;

        // Async: commit n bytes with optional EOF
        { sink.commit(n, eof) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(sink.commit(n, eof)),
            std::error_code>;

        // Async: signal end of data
        { sink.commit_eof() } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(sink.commit_eof()),
            std::error_code>;
    };

} // namespace capy
} // namespace boost

#endif
