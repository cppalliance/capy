//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_BUFFERS_ITERATOR_HPP
#define BOOST_CAPY_BUFFERS_BUFFERS_ITERATOR_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>

#include <compare>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

namespace detail {

// Walking back from the end needs an end iterator, not a sentinel
template<class T>
concept common_buffer_sequence =
    ConstBufferSequence<T> &&
    (ConstBuffer<T> || std::ranges::common_range<T const>);

} // detail

/** A random access iterator over the bytes in a buffer sequence.

    A buffer sequence stores its bytes in several separate regions.
    This iterator presents them as one run of bytes, so standard
    algorithms such as `std::search` can scan across the boundaries
    between buffers without first copying the data into contiguous
    storage. Empty buffers in the sequence are skipped.

    The iterator dereferences to `ByteType&` for a
    @ref MutableBufferSequence and to `ByteType const&` otherwise.

    Moving by `n` bytes walks the underlying buffers, so `operator+=`,
    `operator-=`, `operator+`, `operator-` with an integer, and
    `operator[]` are linear in the number of buffers crossed. Comparison
    and the difference of two iterators are constant time.

    @par Preconditions
    The buffer sequence and the memory it refers to outlive every
    iterator obtained from it. Iterators from different sequences are
    not comparable.

    @par Example
    @code
    std::array<capy::const_buffer, 2> bufs{
        capy::make_buffer(std::string_view("GET / HTTP/1.1\r")),
        capy::make_buffer(std::string_view("\n\r\nbody")) };

    // Find the end of the header, which spans both buffers
    char const crlfcrlf[] = "\r\n\r\n";
    auto it = std::search(
        capy::buffers_begin(bufs), capy::buffers_end(bufs),
        crlfcrlf, crlfcrlf + 4);
    std::size_t header_size = it - capy::buffers_begin(bufs) + 4;
    @endcode

    @tparam BufferSequence The buffer sequence type. It must be a single
    buffer, or a range of buffers whose begin and end have the same type.

    @tparam ByteType The byte type presented by the iterator. Its size
    must be 1.

    @see @ref buffers_begin, @ref buffers_end,
    @ref buffer_sequence_begin
*/
template<class BufferSequence, class ByteType = char>
    requires detail::common_buffer_sequence<BufferSequence>
class buffers_iterator
{
    static_assert(sizeof(ByteType) == 1, "ByteType must be a byte");

    using sequence_iterator = decltype(
        buffer_sequence_begin(std::declval<BufferSequence const&>()));
    using buffer_type = capy::buffer_type<BufferSequence>;
    using byte_type = std::conditional_t<
        MutableBufferSequence<BufferSequence>,
        ByteType,
        ByteType const>;

    buffer_type current_buffer_{};
    std::size_t current_buffer_position_ = 0;
    sequence_iterator current_{};
    sequence_iterator end_{};
    std::size_t position_ = 0;

    // Move to the next non-empty buffer, or to the end of the sequence
    void
    skip_forward() noexcept
    {
        while(current_ != end_)
        {
            current_buffer_ = buffer_type(*current_);
            if(current_buffer_.size() > 0)
                return;
            ++current_;
        }
        current_buffer_ = {};
    }

    // Move to the previous non-empty buffer, positioned past its last byte
    void
    skip_backward() noexcept
    {
        do
        {
            --current_;
            current_buffer_ = buffer_type(*current_);
        }
        while(current_buffer_.size() == 0);
        current_buffer_position_ = current_buffer_.size();
    }

    void
    advance(std::ptrdiff_t n) noexcept
    {
        if(n >= 0)
        {
            auto left = static_cast<std::size_t>(n);
            while(left > 0)
            {
                std::size_t const avail =
                    current_buffer_.size() - current_buffer_position_;
                if(left < avail)
                {
                    current_buffer_position_ += left;
                    position_ += left;
                    return;
                }
                left -= avail;
                position_ += avail;
                ++current_;
                current_buffer_position_ = 0;
                skip_forward();
            }
        }
        else
        {
            auto left = static_cast<std::size_t>(-n);
            while(left > 0)
            {
                if(left <= current_buffer_position_)
                {
                    current_buffer_position_ -= left;
                    position_ -= left;
                    return;
                }
                left -= current_buffer_position_;
                position_ -= current_buffer_position_;
                skip_backward();
            }
        }
    }

public:
    /// The signed type of the distance between two iterators.
    using difference_type = std::ptrdiff_t;

    /// The type of the bytes the iterator visits.
    using value_type = ByteType;

    /// The type returned by `operator->`.
    using pointer = byte_type*;

    /// The type returned by `operator*`.
    using reference = byte_type&;

    /// The iterator category.
    using iterator_category = std::random_access_iterator_tag;

    /// Construct a singular iterator.
    buffers_iterator() = default;

    /** Return an iterator to the first byte of a buffer sequence.

        @param bs The buffer sequence. It must outlive the iterator.

        @return An iterator to the first byte, or the end iterator if
        the sequence holds no bytes.
    */
    static
    buffers_iterator
    begin(BufferSequence const& bs) noexcept
    {
        buffers_iterator it;
        it.current_ = buffer_sequence_begin(bs);
        it.end_ = buffer_sequence_end(bs);
        it.skip_forward();
        return it;
    }

    /** Return an iterator past the last byte of a buffer sequence.

        @param bs The buffer sequence. It must outlive the iterator.

        @return The end iterator.
    */
    static
    buffers_iterator
    end(BufferSequence const& bs) noexcept
    {
        buffers_iterator it;
        it.current_ = buffer_sequence_end(bs);
        it.end_ = it.current_;
        it.position_ = buffer_size(bs);
        return it;
    }

    /** Return a reference to the current byte.

        @par Preconditions
        The iterator is not the end iterator.
    */
    reference
    operator*() const noexcept
    {
        return static_cast<pointer>(
            current_buffer_.data())[current_buffer_position_];
    }

    /** Return a pointer to the current byte.

        @par Preconditions
        The iterator is not the end iterator.
    */
    pointer
    operator->() const noexcept
    {
        return &**this;
    }

    /** Return a reference to the byte `n` positions away.

        @par Complexity
        Linear in the number of buffers crossed.

        @param n The offset from this iterator, in bytes.
    */
    reference
    operator[](difference_type n) const noexcept
    {
        return *(*this + n);
    }

    /// Advance to the next byte.
    buffers_iterator&
    operator++() noexcept
    {
        ++position_;
        if(++current_buffer_position_ < current_buffer_.size())
            return *this;
        ++current_;
        current_buffer_position_ = 0;
        skip_forward();
        return *this;
    }

    /// Advance to the next byte, returning the previous position.
    buffers_iterator
    operator++(int) noexcept
    {
        auto tmp = *this;
        ++*this;
        return tmp;
    }

    /// Move back to the previous byte.
    buffers_iterator&
    operator--() noexcept
    {
        advance(-1);
        return *this;
    }

    /// Move back to the previous byte, returning the previous position.
    buffers_iterator
    operator--(int) noexcept
    {
        auto tmp = *this;
        --*this;
        return tmp;
    }

    /** Move by `n` bytes.

        @par Complexity
        Linear in the number of buffers crossed.

        @param n The number of bytes to move; negative moves backward.

        @return A reference to `*this`.
    */
    buffers_iterator&
    operator+=(difference_type n) noexcept
    {
        advance(n);
        return *this;
    }

    /** Move back by `n` bytes.

        @par Complexity
        Linear in the number of buffers crossed.

        @param n The number of bytes to move back.

        @return A reference to `*this`.
    */
    buffers_iterator&
    operator-=(difference_type n) noexcept
    {
        advance(-n);
        return *this;
    }

    /// Return an iterator `n` bytes after `it`.
    friend
    buffers_iterator
    operator+(buffers_iterator it, difference_type n) noexcept
    {
        it += n;
        return it;
    }

    /// Return an iterator `n` bytes after `it`.
    friend
    buffers_iterator
    operator+(difference_type n, buffers_iterator it) noexcept
    {
        it += n;
        return it;
    }

    /// Return an iterator `n` bytes before `it`.
    friend
    buffers_iterator
    operator-(buffers_iterator it, difference_type n) noexcept
    {
        it -= n;
        return it;
    }

    /// Return the number of bytes from `b` to `a`.
    friend
    difference_type
    operator-(
        buffers_iterator const& a,
        buffers_iterator const& b) noexcept
    {
        return static_cast<difference_type>(a.position_) -
            static_cast<difference_type>(b.position_);
    }

    /// Test whether two iterators refer to the same byte.
    friend
    bool
    operator==(
        buffers_iterator const& a,
        buffers_iterator const& b) noexcept
    {
        return a.position_ == b.position_;
    }

    /// Order two iterators by byte position.
    friend
    std::strong_ordering
    operator<=>(
        buffers_iterator const& a,
        buffers_iterator const& b) noexcept
    {
        return a.position_ <=> b.position_;
    }
};

/** Return an iterator to the first byte of a buffer sequence.

    @param bs The buffer sequence. It must outlive the iterator.

    @return `buffers_iterator<BufferSequence>::begin(bs)`.

    @see @ref buffers_iterator, @ref buffers_end
*/
template<detail::common_buffer_sequence BufferSequence>
buffers_iterator<BufferSequence>
buffers_begin(BufferSequence const& bs) noexcept
{
    return buffers_iterator<BufferSequence>::begin(bs);
}

/// Rejects a temporary sequence, since the iterator would dangle.
template<detail::common_buffer_sequence BufferSequence>
buffers_iterator<BufferSequence>
buffers_begin(BufferSequence const&& bs) = delete;

/** Return an iterator past the last byte of a buffer sequence.

    @param bs The buffer sequence. It must outlive the iterator.

    @return `buffers_iterator<BufferSequence>::end(bs)`.

    @see @ref buffers_iterator, @ref buffers_begin
*/
template<detail::common_buffer_sequence BufferSequence>
buffers_iterator<BufferSequence>
buffers_end(BufferSequence const& bs) noexcept
{
    return buffers_iterator<BufferSequence>::end(bs);
}

/// Rejects a temporary sequence, since the iterator would dangle.
template<detail::common_buffer_sequence BufferSequence>
buffers_iterator<BufferSequence>
buffers_end(BufferSequence const&& bs) = delete;

} // capy
} // boost

#endif
