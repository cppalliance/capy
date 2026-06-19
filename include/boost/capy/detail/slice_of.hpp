//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_SLICE_OF_HPP
#define BOOST_CAPY_DETAIL_SLICE_OF_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>

#include <cstddef>
#include <iterator>
#include <limits>
#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

/** A borrowed view over a byte sub-range of a buffer sequence.

    `slice_of<BS>` is the generic result of `buffer_slice` for a sequence
    that is not closed under sub-ranging (everything except a single
    buffer). It models the same buffer-sequence concept as `BS`
    (`MutableBufferSequence` if `BS` is mutable, otherwise
    `ConstBufferSequence`), so it can be passed anywhere a buffer sequence
    is expected.

    It stores iterators into the underlying sequence plus front/back byte
    offsets; it neither owns nor copies the descriptors. The underlying
    sequence must outlive the view.

    @par Complexity
    Construction is a single forward pass to the cut points: O(buffers up
    to `offset`) for a to-end slice, O(buffers up to `offset + length`)
    for a bounded slice. It never sums the whole sequence.
*/
template<class BS>
    requires MutableBufferSequence<BS> || ConstBufferSequence<BS>
class slice_of
{
public:
    /// The buffer type yielded by iteration.
    using buffer_type = capy::buffer_type<BS>;

    /// The underlying sequence's iterator type.
    using iterator_type =
        decltype(capy::begin(std::declval<BS const&>()));

private:
    iterator_type first_{};
    iterator_type last_{};
    std::size_t   front_skip_ = 0;  // bytes trimmed from *first_
    std::size_t   back_skip_  = 0;  // bytes trimmed from the final buffer

    static buffer_type
    adjust(buffer_type const& b,
        std::size_t front_n, std::size_t back_n) noexcept
    {
        if constexpr (std::is_same_v<buffer_type, mutable_buffer>)
            return mutable_buffer(
                static_cast<char*>(b.data()) + front_n,
                b.size() - front_n - back_n);
        else
            return const_buffer(
                static_cast<char const*>(b.data()) + front_n,
                b.size() - front_n - back_n);
    }

public:
    /// Bidirectional iterator that adjusts the first and last buffers.
    class const_iterator
    {
        iterator_type cur_{};
        iterator_type anchor_first_{};
        iterator_type anchor_last_{};
        std::size_t   front_skip_ = 0;
        std::size_t   back_skip_  = 0;

    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type        = buffer_type;
        using difference_type    = std::ptrdiff_t;
        using pointer            = value_type*;
        using reference          = value_type;

        const_iterator() noexcept = default;

        const_iterator(
            iterator_type cur, iterator_type anchor_first,
            iterator_type anchor_last, std::size_t front_skip,
            std::size_t back_skip) noexcept
            : cur_(cur)
            , anchor_first_(anchor_first)
            , anchor_last_(anchor_last)
            , front_skip_(front_skip)
            , back_skip_(back_skip)
        {
        }

        bool operator==(const_iterator const& o) const noexcept
        {
            return cur_ == o.cur_;
        }

        bool operator!=(const_iterator const& o) const noexcept
        {
            return !(*this == o);
        }

        value_type operator*() const noexcept
        {
            buffer_type buf = *cur_;
            auto const front_n = (cur_ == anchor_first_) ? front_skip_ : 0;
            auto next = cur_;
            ++next;
            auto const back_n = (next == anchor_last_) ? back_skip_ : 0;
            return adjust(buf, front_n, back_n);
        }

        const_iterator& operator++() noexcept { ++cur_; return *this; }
        const_iterator  operator++(int) noexcept
            { auto t = *this; ++*this; return t; }
        const_iterator& operator--() noexcept { --cur_; return *this; }
        const_iterator  operator--(int) noexcept
            { auto t = *this; --*this; return t; }
    };

    /// Construct an empty slice.
    slice_of() noexcept = default;

    /** Construct a view of `[offset, offset + length)` bytes of `bs`.

        @param bs The underlying sequence (must outlive the view).
        @param offset Bytes skipped from the front. Clamped to the total.
        @param length Bytes exposed; the default exposes to the end.
    */
    slice_of(
        BS const& bs,
        std::size_t offset,
        std::size_t length =
            (std::numeric_limits<std::size_t>::max)()) noexcept
    {
        first_ = capy::begin(bs);
        last_  = capy::end(bs);

        // Position first_/front_skip_ at byte `offset` (single forward pass).
        std::size_t skip = offset;
        while (first_ != last_)
        {
            std::size_t const sz = buffer_type(*first_).size();
            if (skip < sz)
            {
                front_skip_ = skip;
                break;
            }
            skip -= sz;
            ++first_;
        }

        if (first_ == last_)
            return;  // offset at or past the end: empty slice
        if (length == (std::numeric_limits<std::size_t>::max)())
            return;  // to-end: last_ already end(bs), back_skip_ == 0

        // Walk `length` live bytes forward to fix last_/back_skip_.
        std::size_t left = length;
        auto cursor = first_;
        std::size_t cursor_front = front_skip_;
        while (cursor != last_ && left > 0)
        {
            std::size_t const sz    = buffer_type(*cursor).size();
            std::size_t const avail = sz - cursor_front;
            if (left <= avail)
            {
                back_skip_ = avail - left;
                ++cursor;
                last_ = cursor;
                return;
            }
            left -= avail;
            ++cursor;
            cursor_front = 0;
        }
        last_ = cursor;
    }

    /** Construct directly from a positioned iterator range.

        Used by `consuming_buffers::data()` to expose its current position
        without re-walking from the start.
    */
    slice_of(
        iterator_type first, iterator_type last,
        std::size_t front_skip = 0, std::size_t back_skip = 0) noexcept
        : first_(first)
        , last_(last)
        , front_skip_(front_skip)
        , back_skip_(back_skip)
    {
    }

    /// Return an iterator to the first buffer.
    const_iterator begin() const noexcept
    {
        return const_iterator(
            first_, first_, last_, front_skip_, back_skip_);
    }

    /// Return an iterator past the last buffer.
    const_iterator end() const noexcept
    {
        return const_iterator(
            last_, first_, last_, front_skip_, back_skip_);
    }
};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
