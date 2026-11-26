//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Andrzej Krzemieński
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_HPP
#define BOOST_CAPY_BUFFERS_HPP

#include <boost/capy/detail/config.hpp>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <type_traits>

// https://www.boost.org/doc/libs/1_65_0/doc/html/boost_asio/reference/ConstBufferSequence.html

namespace boost {

namespace asio {
class const_buffer;
class mutable_buffer;
} // asio

namespace capy {

class const_buffer;
class mutable_buffer;

/** A reference to a contiguous region of writable memory.

    Represents a pointer and size pair for a modifiable byte range.
    Does not own, allocate, or copy the memory: copying a buffer copies
    the pointer and size only. The memory must stay valid until every
    operation using the buffer completes. Satisfies
    `MutableBufferSequence` (as a single-element sequence) and is
    implicitly convertible to `const_buffer`.

    @see @ref const_buffer, @ref MutableBufferSequence
*/
class mutable_buffer
{
    unsigned char* p_ = nullptr;
    std::size_t n_ = 0;

public:
    /// Construct an empty buffer.
    mutable_buffer() = default;

    /** Construct a copy.

        @param other The buffer to copy.
    */
    mutable_buffer(
        mutable_buffer const& other) = default;

    /** Assign by copying.

        @param other The buffer to copy.

        @return A reference to `*this`.
    */
    mutable_buffer& operator=(
        mutable_buffer const& other) = default;

    /** Construct from a pointer and size.

        Takes `void*` so a pointer to any object type binds without a
        cast, since the buffer represents a raw, untyped writable
        region. Stored internally as `unsigned char*` for byte-wise
        pointer arithmetic (see `operator+=`).

        @param data A pointer to the first byte of the region.

        @param size The size of the region, in bytes.
    */
    constexpr mutable_buffer(
        void* data, std::size_t size) noexcept
        : p_(static_cast<unsigned char*>(data))
        , n_(size)
    {
    }

    /** Return a pointer to the memory region.

        Returns `void*`, symmetric with the constructor, so the
        caller can reinterpret the raw region as whatever type it needs.

        @return A pointer to the first byte of the region.
    */
    constexpr void* data() const noexcept
    {
        return p_;
    }

    /** Return the size in bytes.

        @return The size of the region, in bytes.
    */
    constexpr std::size_t size() const noexcept
    {
        return n_;
    }

    /** Advance the buffer start, shrinking the region.

        @param n Bytes to skip. Clamped to `size()`.

        @return A reference to `*this`.
    */
    mutable_buffer&
    operator+=(std::size_t n) noexcept
    {
        if( n > n_)
            n = n_;
        p_ += n;
        n_ -= n;
        return *this;
    }
};

/** A reference to a contiguous region of read-only memory.

    Represents a pointer and size pair for a non-modifiable byte range.
    Does not own, allocate, or copy the memory: copying a buffer copies
    the pointer and size only. The memory must stay valid until every
    operation using the buffer completes. Satisfies
    `ConstBufferSequence` (as a single-element sequence). Implicitly
    constructible from `mutable_buffer`.

    @see @ref mutable_buffer, @ref ConstBufferSequence
*/
class const_buffer
{
    unsigned char const* p_ = nullptr;
    std::size_t n_ = 0;

public:
    /// Construct an empty buffer.
    const_buffer() = default;

    /** Construct a copy.

        @param other The buffer to copy.
    */
    const_buffer(const_buffer const& other) = default;

    /** Assign by copying.

        @param other The buffer to copy.

        @return A reference to `*this`.
    */
    const_buffer& operator=(
        const_buffer const& other) = default;

    /** Construct from a pointer and size.

        Takes `void const*` so a pointer to any object type binds
        without a cast, since the buffer represents a raw, untyped
        read-only region. Stored internally as `unsigned char const*`
        for byte-wise pointer arithmetic (see `operator+=`).

        @param data A pointer to the first byte of the region.

        @param size The size of the region, in bytes.
    */
    constexpr const_buffer(
        void const* data, std::size_t size) noexcept
        : p_(static_cast<unsigned char const*>(data))
        , n_(size)
    {
    }

    /** Construct from mutable_buffer.

        @param b The writable buffer whose region is referenced.
    */
    constexpr const_buffer(
        mutable_buffer const& b) noexcept
        : p_(static_cast<unsigned char const*>(b.data()))
        , n_(b.size())
    {
    }

    /** Return a pointer to the memory region.

        Returns `void const*`, symmetric with the constructor, so the
        caller can reinterpret the raw region as whatever type it needs.

        @return A pointer to the first byte of the region.
    */
    constexpr void const* data() const noexcept
    {
        return p_;
    }

    /** Return the size in bytes.

        @return The size of the region, in bytes.
    */
    constexpr std::size_t size() const noexcept
    {
        return n_;
    }

    /** Advance the buffer start, shrinking the region.

        @param n Bytes to skip. Clamped to `size()`.

        @return A reference to `*this`.
    */
    const_buffer&
    operator+=(std::size_t n) noexcept
    {
        if( n > n_)
            n = n_;
        p_ += n;
        n_ -= n;
        return *this;
    }
};

/** Type recognized by the library as a _buffer_ (a handle to contiguous memory region)
    with const access to data. 

    @par Semantic Requirements 

    After the conversion to @ref const_buffer `b`, `b.data()` and `b.size()` 
    designate a valid contiguous memory region. The region shall remain valid
    for the duration of an operation represented by the concept-constrained
    function template call.

    @note Function templates in this library constrained by this concept,
    such as @ref buffer_size, are often `noexcept`, so if this conversion
    throws, this results in calling `std::terminate()`.  

    @note `std::convertible_to` implies that @ref const_buffer
    can also be explicitly constructed from `T const&` with the same effect as the
    implicit conversion.

    @see @ref const_buffer, @ref MutableBuffer
*/
template<typename T>
concept ConstBuffer =
    std::convertible_to<T const&, const_buffer>;

/** Type recognized by the library as a _buffer_ (a handle to contiguous memory region)
    with mutable access to data. 

    @par Semantic Requirements 

    After the conversion to @ref mutable_buffer or @ref const_buffer `b`, `b.data()` and `b.size()` 
    designate a valid contiguous memory region. The region shall remain valid
    for the duration of an operation represented by the concept-constrained
    function template call.

    @note Function templates in this library constrained by this concept,
    such as @ref buffer_size, are often `noexcept`, so if this conversion
    throws, this results in calling `std::terminate()`.  

    @note `std::convertible_to` implies that @ref mutable_buffer
    can also be explicitly constructed from `T const&` with the same effect as the
    implicit conversion.

    @see @ref mutable_buffer, @ref ConstBuffer
*/
template<typename T>
concept MutableBuffer =
    ConstBuffer<T> && std::convertible_to<T const&, mutable_buffer>;

/** Type recognized by the library as a _sequence_ of buffers with const access to data:
    either a type modelling @ref ConstBuffer or a range thereof.

    @par Semantic Requirements
    
    A copy-constructed buffer sequence shall refer to the same contiguous memory regions as the original.
    This can be expressed as:
    
    @code
    bool copy_postcondition(ConstBufferSequence auto const& x)
    {
      ConstBufferSequence auto y = x;
      return std::equal(
        buffer_sequence_begin(x), buffer_sequence_end(x),
        buffer_sequence_begin(y), buffer_sequence_end(y),
        [](const const_buffer& bx, const const_buffer& by)
        {
          return bx.data() == by.data()
              && bx.size() == by.size();
        }
      );
   }
    @endcode

    If a type models both @ref ConstBuffer and a range thereof,
    then it is treated as @ref ConstBuffer.

    @note Function templates in this library constrained by this concept,
    such as @ref buffer_size, are often `noexcept`, so if the operations used
    to iterate over the buffer range -- such as `.begin()` or the iterator API --
    throw, this results in calling `std::terminate()`.
    
    @see @ref const_buffer, @ref MutableBufferSequence
*/
// tag::const_buffer_sequence_concept[]
template<typename T>
concept ConstBufferSequence =
    std::copy_constructible<T> && (
    ConstBuffer<T> || (
        std::ranges::bidirectional_range<T> &&
        ConstBuffer<std::ranges::range_value_t<T>>));
// end::const_buffer_sequence_concept[]

/** Type recognized by the library as a _sequence_ of buffers with mutable access to data:
    either a type modelling @ref MutableBuffer or a range thereof.

    @par Semantic Requirements
    
    If a type models both @ref MutableBuffer and a range thereof,
    then it is treated as @ref MutableBuffer.

    @note A type satisfying @ref ConstBuffer is never considered to be a range of
    mutable buffers.

    @note Function templates in this library constrained by this concept,
    such as @ref buffer_size, are often `noexcept`, so if the operations used
    to iterate over the buffer range — such as `.begin()` or the iterator API —
    throw, this results in calling `std::terminate()`.
    
    @see @ref mutable_buffer, @ref ConstBufferSequence
*/
// tag::mutable_buffer_sequence_concept[]
template<typename T>
concept MutableBufferSequence =
    ConstBufferSequence<T> && (MutableBuffer<T> || (
        !ConstBuffer<T> &&
        std::ranges::bidirectional_range<T> &&
        MutableBuffer<std::ranges::range_value_t<T>>));
// end::mutable_buffer_sequence_concept[]

/** Return an iterator to the first buffer in a sequence.

    @functionobject
*/
constexpr struct
{
    /** Return a pointer to a single buffer, forming a one-element range.

        @param b A single buffer.

        @return A pointer to `b`.
    */
    template<ConstBuffer ConvertibleToBuffer>
    auto operator()(ConvertibleToBuffer const& b) const noexcept -> ConvertibleToBuffer const*
    {
        return std::addressof(b);
    }

    /** Return an iterator to the first buffer of a sequence.

        @param bs The buffer sequence.

        @return An iterator to the first buffer of `bs`.
    */
    template<ConstBufferSequence BS>
        requires (!ConstBuffer<BS>)
    auto operator()(BS const& bs) const noexcept
    {
        return std::ranges::begin(bs);
    }

    /** Return an iterator to the first buffer of a sequence.

        @param bs The buffer sequence.

        @return An iterator to the first buffer of `bs`.
    */
    template<ConstBufferSequence BS>
        requires (!ConstBuffer<BS>)
    auto operator()(BS& bs) const noexcept
    {
        return std::ranges::begin(bs);
    }
} buffer_sequence_begin {};

/** Return an iterator past the last buffer in a sequence.

    @functionobject
*/
constexpr struct
{
    /** Return a pointer one past a single buffer, forming a one-element range.

        @param b A single buffer.

        @return A pointer one past `b`.
    */
    template<ConstBuffer ConvertibleToBuffer>
    auto operator()(ConvertibleToBuffer const& b) const noexcept -> ConvertibleToBuffer const*
    {
        return std::addressof(b) + 1;
    }

    /** Return an iterator past the last buffer of a sequence.

        @param bs The buffer sequence.

        @return An iterator one past the last buffer of `bs`.
    */
    template<ConstBufferSequence BS>
        requires (!ConstBuffer<BS>)
    auto operator()(BS const& bs) const noexcept
    {
        return std::ranges::end(bs);
    }

    /** Return an iterator past the last buffer of a sequence.

        @param bs The buffer sequence.

        @return An iterator one past the last buffer of `bs`.
    */
    template<ConstBufferSequence BS>
        requires (!ConstBuffer<BS>)
    auto operator()(BS& bs) const noexcept
    {
        return std::ranges::end(bs);
    }
} buffer_sequence_end {};

/** Return the total byte count across all buffers in a sequence.

    @functionobject
*/
constexpr struct
{
    // GCC 13 falsely flags reads of arr_[i].n_ in detail::buffer_array
    // when iterating here. The class uses union storage with placement
    // new for slots 0..n_-1, so reads inside this bounded loop are
    // well-defined, but the optimizer can't prove the loop bound and
    // warns. The runtime cost of value-initializing all N slots is
    // non-trivial for non-trivial value types, so we suppress instead.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    /** Return the total byte count across all buffers in a sequence.

        Sums the `size()` of each buffer in the sequence. This differs
        from `buffers_count` which counts the number of buffer elements.

        @param bs The buffer sequence.

        @return The sum of the sizes of all buffers in `bs`.

        @par Example
        @par !example example

    */
    template<ConstBufferSequence CB>
    constexpr std::size_t operator()(
        CB const& bs) const noexcept
    {
        std::size_t n = 0;
        auto const e = capy::buffer_sequence_end(bs);
        for(auto it = capy::buffer_sequence_begin(bs); it != e; ++it)
            n += const_buffer(*it).size();
        return n;
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
} buffer_size {};

/** Check if a buffer sequence contains no data.

    @functionobject
*/
constexpr struct
{
    // See note on buffer_size above — same union-storage false positive.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    /** Check if a buffer sequence contains no data.

        @param bs The buffer sequence.

        @return `true` if all buffers have size zero or the sequence
            is empty.
    */
    template<ConstBufferSequence CB>
    constexpr bool operator()(
        CB const& bs) const noexcept
    {
        auto it = buffer_sequence_begin(bs);
        auto const end_ = buffer_sequence_end(bs);
        while(it != end_)
        {
            const_buffer b(*it++);
            if(b.size() != 0)
                return false;
        }
        return true;
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
} buffers_empty {};

namespace detail {

template<class It>
auto
length_impl(It first, It last, int)
    -> decltype(static_cast<std::size_t>(last - first))
{
    return static_cast<std::size_t>(last - first);
}

template<class It>
std::size_t
length_impl(It first, It last, long)
{
    std::size_t n = 0;
    while(first != last)
    {
        ++first;
        ++n;
    }
    return n;
}

} // detail

/** Return the number of buffer elements in a sequence.

    Counts the number of individual buffer objects, not bytes.
    For a single buffer, returns 1. For a range, returns the
    distance from `buffer_sequence_begin` to `buffer_sequence_end`.

    @param bs The buffer sequence.

    @return The number of buffers in `bs`.

    @see @ref buffer_size
*/
template<ConstBufferSequence CB>
std::size_t
buffers_count(CB const& bs)
{
    return detail::length_impl(
        buffer_sequence_begin(bs), buffer_sequence_end(bs), 0);
}

/// Names `mutable_buffer` for a mutable sequence, `const_buffer` otherwise.
template<typename BS>
using buffer_type = std::conditional_t<
    MutableBufferSequence<BS>,
    mutable_buffer, const_buffer>;

} // capy
} // boost

#endif
