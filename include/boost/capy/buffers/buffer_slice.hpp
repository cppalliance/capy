//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_BUFFER_SLICE_HPP
#define BOOST_CAPY_BUFFERS_BUFFER_SLICE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/detail/slice_impl.hpp>

#include <cstddef>
#include <limits>

namespace boost {
namespace capy {

/** Return a byte-range slice of a buffer sequence.

    Constructs a view over a contiguous byte range of `seq`. The
    slice exposes its current bytes via `data()` (a buffer sequence)
    and supports incremental consumption via `remove_prefix(n)`.

    @par Return Value
    An object of unspecified type satisfying the @ref Slice concept.
    Bind with `auto` and operate through the concept's members. When
    `seq` models @ref MutableBufferSequence, the returned object
    additionally models @ref MutableSlice.

    @par Lifetime
    The returned slice is associated with `seq` as its underlying
    buffer sequence. `seq` — and the memory referenced by its buffer
    descriptors — must remain valid for as long as the slice, or
    any buffer sequence obtained from its `data()`, is in use.
    Passing a temporary buffer sequence to `buffer_slice` produces
    a dangling slice.

    The buffer sequence returned by `data()` is independent of the
    slice object: subsequent operations on the slice (mutation,
    copy, move, destruction) do not invalidate an already-obtained
    `data()` view. It remains valid for as long as `seq` is valid.

    Iterators and buffer descriptors obtained through `data()`
    follow the same invalidation rules as those of `seq`.

    @par Parameters
    @li `seq` The underlying buffer sequence. Must outlive the
        returned slice and any `data()` view obtained from it.
    @li `offset` Number of bytes to skip from the start of `seq`.
        Clamped to `buffer_size(seq)`.
    @li `length` Maximum number of bytes the slice will expose,
        starting at `offset`. Clamped to `buffer_size(seq) - offset`.
        Defaults to the maximum value of `std::size_t`, i.e. "to end".

    @par Example
    @code
    template< ReadStream Stream, MutableBufferSequence MB >
    task< io_result< std::size_t > >
    read_all( Stream& stream, MB buffers )
    {
        auto s = buffer_slice( buffers );
        std::size_t const total_size = buffer_size( buffers );
        std::size_t total = 0;
        while( total < total_size )
        {
            auto [ec, n] = co_await stream.read_some( s.data() );
            s.remove_prefix( n );
            total += n;
            if( ec )
                co_return {ec, total};
        }
        co_return {{}, total};
    }
    @endcode

    @see Slice, MutableSlice
*/
template<class BufferSequence>
    requires MutableBufferSequence<BufferSequence>
          || ConstBufferSequence<BufferSequence>
auto
buffer_slice(
    BufferSequence const& seq,
    std::size_t offset = 0,
    std::size_t length =
        (std::numeric_limits<std::size_t>::max)()) noexcept
{
    return detail::slice_impl<BufferSequence>(seq, offset, length);
}

/** Deleted overload that rejects rvalue arguments at compile time.

    Because the returned slice's validity depends on the underlying
    buffer sequence remaining alive, calling `buffer_slice` with a
    temporary buffer sequence would produce an immediately dangling
    slice. This overload makes such calls ill-formed, surfacing the
    lifetime error at compile time rather than as runtime UB.

    To slice a buffer sequence produced as a temporary, hoist it
    into a named variable first:

    @code
    auto bufs = some_dynamic_buffer.data();   // named, lives in scope
    auto s = buffer_slice( bufs );            // OK
    @endcode
*/
template<class BufferSequence>
    requires MutableBufferSequence<BufferSequence>
          || ConstBufferSequence<BufferSequence>
auto
buffer_slice(
    BufferSequence const&& seq,
    std::size_t offset = 0,
    std::size_t length =
        (std::numeric_limits<std::size_t>::max)()) = delete;

} // namespace capy
} // namespace boost

#endif
