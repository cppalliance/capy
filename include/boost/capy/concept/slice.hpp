//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_SLICE_HPP
#define BOOST_CAPY_CONCEPT_SLICE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>

#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types that view a byte sub-range of a buffer sequence.

    A type satisfies `Slice` if it provides a view over a contiguous
    byte range within an underlying buffer sequence, with an operation
    to advance the start and exposure of the current bytes as a buffer
    sequence.

    @par Syntactic Requirements
    @li `cs.data()` returns a @ref ConstBufferSequence
    @li `s.remove_prefix(n)` advances the start of the slice by `n` bytes

    @par Semantic Requirements
    @li `s.data()` returns a buffer sequence view of the slice's current
        live bytes.
    @li `s.remove_prefix(n)` makes the first `min(n, total_live_bytes)`
        bytes no longer part of the slice.

    @par Lifetime
    A `Slice` is associated, on construction, with an underlying
    buffer sequence. The slice is valid for as long as that sequence
    — and the memory referenced by its buffer descriptors — remains
    valid. Operating on a slice whose underlying sequence is no
    longer valid is undefined behavior.

    The buffer sequence returned by `data()` is independent of the
    slice object: subsequent operations on the slice
    (`remove_prefix`, copy, move, destruction) do not invalidate
    an already-obtained `data()` view. It remains valid for as
    long as the slice's underlying buffer sequence is valid.

    Buffer descriptors obtained through `data()` follow the same
    invalidation rules as those of the underlying sequence.

    @par Concrete Types
    Objects modeling `Slice` are produced by the @ref buffer_slice free
    function. The concrete type returned by `buffer_slice` is unspecified;
    user code should bind it with `auto` and rely on this concept. When
    the underlying buffer sequence models @ref MutableBufferSequence, the
    returned object additionally models @ref MutableSlice.

    @par Example
    @code
    template< WriteStream Stream, Slice S >
    task<> write_all( Stream& stream, S s, std::size_t total )
    {
        std::size_t sent = 0;
        while( sent < total )
        {
            auto [ec, n] = co_await stream.write_some( s.data() );
            s.remove_prefix( n );
            sent += n;
            if( ec )
                co_return;
        }
    }
    @endcode

    @see buffer_slice, MutableSlice, ConstBufferSequence
*/
template<typename T>
concept Slice =
    requires(T& s, T const& cs, std::size_t n)
    {
        { cs.data() } -> ConstBufferSequence;
        s.remove_prefix(n);
    };

/** Concept for slices whose `data()` exposes writable buffers.

    A type satisfies `MutableSlice` if it satisfies @ref Slice and
    its `data()` member additionally returns a
    @ref MutableBufferSequence. This is the slice analog of the
    @ref MutableBufferSequence refinement of @ref ConstBufferSequence.

    Use `MutableSlice` to constrain generic code that needs to pass
    the slice's current bytes to a @ref ReadStream's `read_some` or
    any other operation requiring write access through the buffer
    sequence.

    @par Producing a MutableSlice
    @ref buffer_slice returns an object modeling `MutableSlice` when
    the input buffer sequence models @ref MutableBufferSequence. When
    the input is only @ref ConstBufferSequence, the returned object
    models @ref Slice but not `MutableSlice`.

    @par Example
    @code
    template< ReadStream Stream, MutableSlice S >
    task<> read_all( Stream& stream, S s, std::size_t total )
    {
        std::size_t received = 0;
        while( received < total )
        {
            auto [ec, n] = co_await stream.read_some( s.data() );
            s.remove_prefix( n );
            received += n;
            if( ec )
                co_return;
        }
    }
    @endcode

    @see Slice, buffer_slice, MutableBufferSequence
*/
template<typename T>
concept MutableSlice =
    Slice<T> &&
    requires(T const& cs)
    {
        { cs.data() } -> MutableBufferSequence;
    };

} // namespace capy
} // namespace boost

#endif
