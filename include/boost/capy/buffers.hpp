//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
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
    Does not own the memory. Satisfies `MutableBufferSequence` (as a
    single-element sequence) and is implicitly convertible to
    `const_buffer`.

    @see const_buffer, MutableBufferSequence
*/
class mutable_buffer
{
    unsigned char* p_ = nullptr;
    std::size_t n_ = 0;

public:
    /// Construct an empty buffer.
    mutable_buffer() = default;

    /// Construct a copy.
    mutable_buffer(
        mutable_buffer const&) = default;

    /// Assign by copying.
    mutable_buffer& operator=(
        mutable_buffer const&) = default;

    /// Construct from pointer and size.
    constexpr mutable_buffer(
        void* data, std::size_t size) noexcept
        : p_(static_cast<unsigned char*>(data))
        , n_(size)
    {
    }

    /// Return a pointer to the memory region.
    constexpr void* data() const noexcept
    {
        return p_;
    }

    /// Return the size in bytes.
    constexpr std::size_t size() const noexcept
    {
        return n_;
    }

    /** Advance the buffer start, shrinking the region.

        @param n Bytes to skip. Clamped to `size()`.
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
    Does not own the memory. Satisfies `ConstBufferSequence` (as a
    single-element sequence). Implicitly constructible from
    `mutable_buffer`.

    @see mutable_buffer, ConstBufferSequence
*/
class const_buffer
{
    unsigned char const* p_ = nullptr;
    std::size_t n_ = 0;

public:
    /// Construct an empty buffer.
    const_buffer() = default;

    /// Construct a copy.
    const_buffer(const_buffer const&) = default;

    /// Assign by copying.
    const_buffer& operator=(
        const_buffer const& other) = default;

    /// Construct from pointer and size.
    constexpr const_buffer(
        void const* data, std::size_t size) noexcept
        : p_(static_cast<unsigned char const*>(data))
        , n_(size)
    {
    }

    /// Construct from mutable_buffer.
    constexpr const_buffer(
        mutable_buffer const& b) noexcept
        : p_(static_cast<unsigned char const*>(b.data()))
        , n_(b.size())
    {
    }

    /// Return a pointer to the memory region.
    constexpr void const* data() const noexcept
    {
        return p_;
    }

    /// Return the size in bytes.
    constexpr std::size_t size() const noexcept
    {
        return n_;
    }

    /** Advance the buffer start, shrinking the region.

        @param n Bytes to skip. Clamped to `size()`.
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

/** Concept for sequences of read-only buffer regions.

    A type satisfies `ConstBufferSequence` if it represents one or more
    contiguous memory regions that can be read. This includes single
    buffers (convertible to `const_buffer`) and ranges of buffers.

    @par Syntactic Requirements
    @li Convertible to `const_buffer`, OR
    @li A bidirectional range with value type convertible to `const_buffer`

    @see const_buffer, MutableBufferSequence
*/
template<typename T>
concept ConstBufferSequence =
    std::is_convertible_v<T, const_buffer> || (
        std::ranges::bidirectional_range<T> &&
        std::is_convertible_v<std::ranges::range_value_t<T>, const_buffer>);

/** Concept for sequences of writable buffer regions.

    A type satisfies `MutableBufferSequence` if it represents one or more
    contiguous memory regions that can be written. This includes single
    buffers (convertible to `mutable_buffer`) and ranges of buffers.
    Every `MutableBufferSequence` also satisfies `ConstBufferSequence`.

    @par Syntactic Requirements
    @li Convertible to `mutable_buffer`, OR
    @li A bidirectional range with value type convertible to `mutable_buffer`

    @see mutable_buffer, ConstBufferSequence
*/
template<typename T>
concept MutableBufferSequence =
    std::is_convertible_v<T, mutable_buffer> || (
        std::ranges::bidirectional_range<T> &&
        std::is_convertible_v<std::ranges::range_value_t<T>, mutable_buffer>);

/** Return an iterator to the first buffer in a sequence.

    Handles single buffers and ranges uniformly. For a single buffer,
    returns a pointer to it (forming a one-element range).
*/
constexpr struct begin_mrdocs_workaround_t
{
    template<std::convertible_to<const_buffer> ConvertibleToBuffer>
    auto operator()(ConvertibleToBuffer const& b) const noexcept -> ConvertibleToBuffer const*
    {
        return std::addressof(b);
    }

    template<ConstBufferSequence BS>
        requires (!std::convertible_to<BS, const_buffer>)
    auto operator()(BS const& bs) const noexcept
    {
        return std::ranges::begin(bs);
    }

    template<ConstBufferSequence BS>
        requires (!std::convertible_to<BS, const_buffer>)
    auto operator()(BS& bs) const noexcept
    {
        return std::ranges::begin(bs);
    }
} begin {};

/** Return an iterator past the last buffer in a sequence.

    Handles single buffers and ranges uniformly. For a single buffer,
    returns a pointer one past it.
*/
constexpr struct end_mrdocs_workaround_t
{
    template<std::convertible_to<const_buffer> ConvertibleToBuffer>
    auto operator()(ConvertibleToBuffer const& b) const noexcept -> ConvertibleToBuffer const*
    {
        return std::addressof(b) + 1;
    }

    template<ConstBufferSequence BS>
        requires (!std::convertible_to<BS, const_buffer>)
    auto operator()(BS const& bs) const noexcept
    {
        return std::ranges::end(bs);
    }

    template<ConstBufferSequence BS>
        requires (!std::convertible_to<BS, const_buffer>)
    auto operator()(BS& bs) const noexcept
    {
        return std::ranges::end(bs);
    }
} end {};

/** Return the total byte count across all buffers in a sequence.

    Sums the `size()` of each buffer in the sequence. This differs
    from `buffer_length` which counts the number of buffer elements.

    @par Example
    @code
    std::array<mutable_buffer, 2> bufs = { ... };
    std::size_t total = buffer_size( bufs );  // sum of both sizes
    @endcode
*/
constexpr struct buffer_size_mrdocs_workaround_t
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
    template<ConstBufferSequence CB>
    constexpr std::size_t operator()(
        CB const& bs) const noexcept
    {
        std::size_t n = 0;
        auto const e = capy::end(bs);
        for(auto it = capy::begin(bs); it != e; ++it)
            n += const_buffer(*it).size();
        return n;
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
} buffer_size {};

/** Check if a buffer sequence contains no data.

    @return `true` if all buffers have size zero or the sequence
        is empty.
*/
constexpr struct buffer_empty_mrdocs_workaround_t
{
    // See note on buffer_size above — same union-storage false positive.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    template<ConstBufferSequence CB>
    constexpr bool operator()(
        CB const& bs) const noexcept
    {
        auto it = begin(bs);
        auto const end_ = end(bs);
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
} buffer_empty {};

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
    distance from `begin` to `end`.

    @see buffer_size
*/
template<ConstBufferSequence CB>
std::size_t
buffer_length(CB const& bs)
{
    return detail::length_impl(
        begin(bs), end(bs), 0);
}

/// Alias for `mutable_buffer` or `const_buffer` based on sequence type.
template<typename BS>
using buffer_type = std::conditional_t<
    MutableBufferSequence<BS>,
    mutable_buffer, const_buffer>;

} // capy
} // boost

#endif
