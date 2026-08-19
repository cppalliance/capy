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
#include <boost/capy/detail/slice_of.hpp>

#include <concepts>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace boost {
namespace capy {

/** Names whichever buffer type `buffer_slice` returns for a sequence `BS`.

    A single buffer is closed under sub-ranging, so slicing it yields a
    buffer of the same kind. Any other sequence yields the generic
    `detail::slice_of<BS>` borrowed view. In both cases the result is itself
    a buffer sequence — `slice_type<BS> ∈ { buffer, slice_of<BS> }`.
*/
template<class BS>
using slice_type = std::conditional_t<
    std::convertible_to<BS, const_buffer>,
    buffer_type<BS>,
    detail::slice_of<BS>>;

/** Return a byte sub-range of a buffer sequence, as a value.

    The result is itself a buffer sequence (`slice_type<BS>`), so pass it
    directly to any operation expecting a buffer sequence. There is no
    `.data()` and no separate concept to bind. For a single buffer the
    result is an adjusted buffer; for any other sequence it is a borrowed
    `slice_of<BS>` view.

    @par Lifetime
    Except for the single-buffer case, the result borrows `seq`: it stores
    iterators into the sequence, not a copy. `seq` must outlive the result.
    The rvalue overload is deleted so a temporary cannot be sliced into a
    dangling view.

    @par Complexity
    Single forward pass to the cut points; never sums the whole sequence.

    @param seq The sequence to slice. Must outlive the result.
    @param offset Bytes skipped from the front. Clamped to the total size.
    @param length Bytes exposed, starting at `offset`. Defaults to the end.

    @return A `slice_type<BS>` value modeling the same buffer-sequence
        concept as `seq` (mutable if `seq` is mutable).

    @par Example
    @code
    co_await write(sock, buffer_slice(bufs, 0, 16384));  // first 16 KB
    auto rest = buffer_slice(bufs, n);                   // drop first n
    @endcode

    @see slice_type, consuming_buffers
*/
template<class BufferSequence>
    requires MutableBufferSequence<BufferSequence>
          || ConstBufferSequence<BufferSequence>
slice_type<BufferSequence>
buffer_slice(
    BufferSequence const& seq,
    std::size_t offset = 0,
    std::size_t length =
        (std::numeric_limits<std::size_t>::max)()) noexcept
{
    if constexpr (std::convertible_to<BufferSequence, const_buffer>)
    {
        // A single buffer is its own slice: advance and (maybe) truncate.
        buffer_type<BufferSequence> b = seq;
        b += offset;  // operator+= clamps to size()
        if (length < b.size())
            b = buffer_type<BufferSequence>(b.data(), length);
        return b;
    }
    else
    {
        return detail::slice_of<BufferSequence>(seq, offset, length);
    }
}

/** Rejects a temporary sequence at compile time, since the result would dangle.

    Slicing a temporary would yield an immediately dangling view (the
    result borrows the sequence). Hoist the sequence into a named variable
    first.
*/
template<class BufferSequence>
    requires MutableBufferSequence<BufferSequence>
          || ConstBufferSequence<BufferSequence>
slice_type<BufferSequence>
buffer_slice(
    BufferSequence const&& seq,
    std::size_t offset = 0,
    std::size_t length =
        (std::numeric_limits<std::size_t>::max)()) = delete;

} // namespace capy
} // namespace boost

#endif
