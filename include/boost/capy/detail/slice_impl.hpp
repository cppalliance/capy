//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

/*
    Implementation type for the public buffer_slice() free function.
    Users see this only via auto + the Slice concept; the type is
    documented as unspecified. Maintained alongside Slice in
    include/boost/capy/concept/slice.hpp.
*/

#ifndef BOOST_CAPY_DETAIL_SLICE_IMPL_HPP
#define BOOST_CAPY_DETAIL_SLICE_IMPL_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

template<class T>
struct slice_buffer_type_for;

template<MutableBufferSequence T>
struct slice_buffer_type_for<T>
{
    using type = mutable_buffer;
};

template<ConstBufferSequence T>
    requires (!MutableBufferSequence<T>)
struct slice_buffer_type_for<T>
{
    using type = const_buffer;
};

template<class BufferSequence>
    requires MutableBufferSequence<BufferSequence>
          || ConstBufferSequence<BufferSequence>
class slice_impl
{
public:
    using iterator_type =
        decltype(capy::begin(std::declval<BufferSequence const&>()));
    using end_iterator_type =
        decltype(capy::end(std::declval<BufferSequence const&>()));
    using buffer_type =
        typename slice_buffer_type_for<BufferSequence>::type;

private:
    iterator_type first_{};
    end_iterator_type last_{};
    std::size_t front_skip_ = 0;
    std::size_t back_skip_ = 0;

    static buffer_type adjust_buffer(
        buffer_type const& buf,
        std::size_t front_n,
        std::size_t back_n) noexcept
    {
        if constexpr (std::is_same_v<buffer_type, mutable_buffer>)
        {
            return mutable_buffer(
                static_cast<char*>(buf.data()) + front_n,
                buf.size() - front_n - back_n);
        }
        else
        {
            return const_buffer(
                static_cast<char const*>(buf.data()) + front_n,
                buf.size() - front_n - back_n);
        }
    }

public:
    /// View returned by `slice_impl::data()`.
    class data_view
    {
        iterator_type first_{};
        end_iterator_type last_{};
        std::size_t front_skip_ = 0;
        std::size_t back_skip_ = 0;

    public:
        class const_iterator
        {
            iterator_type cur_{};
            iterator_type anchor_first_{};
            end_iterator_type anchor_last_{};
            std::size_t front_skip_ = 0;
            std::size_t back_skip_ = 0;

        public:
            using iterator_category = std::bidirectional_iterator_tag;
            using value_type = buffer_type;
            using difference_type = std::ptrdiff_t;
            using pointer = value_type*;
            using reference = value_type;

            const_iterator() noexcept = default;

            const_iterator(
                iterator_type cur,
                iterator_type anchor_first,
                end_iterator_type anchor_last,
                std::size_t front_skip,
                std::size_t back_skip) noexcept
                : cur_(cur)
                , anchor_first_(anchor_first)
                , anchor_last_(anchor_last)
                , front_skip_(front_skip)
                , back_skip_(back_skip)
            {
            }

            bool operator==(const_iterator const& other) const noexcept
            {
                return cur_ == other.cur_;
            }

            bool operator!=(const_iterator const& other) const noexcept
            {
                return !(*this == other);
            }

            value_type operator*() const noexcept
            {
                buffer_type buf = *cur_;
                auto front_n = (cur_ == anchor_first_) ? front_skip_ : 0;
                auto next = cur_;
                ++next;
                auto back_n = (next == anchor_last_) ? back_skip_ : 0;
                return adjust_buffer(buf, front_n, back_n);
            }

            const_iterator& operator++() noexcept
            {
                ++cur_;
                return *this;
            }

            const_iterator operator++(int) noexcept
            {
                const_iterator tmp = *this;
                ++*this;
                return tmp;
            }

            const_iterator& operator--() noexcept
            {
                --cur_;
                return *this;
            }

            const_iterator operator--(int) noexcept
            {
                const_iterator tmp = *this;
                --*this;
                return tmp;
            }
        };

        data_view() noexcept = default;

        data_view(
            iterator_type first,
            end_iterator_type last,
            std::size_t front_skip,
            std::size_t back_skip) noexcept
            : first_(first)
            , last_(last)
            , front_skip_(front_skip)
            , back_skip_(back_skip)
        {
        }

        const_iterator begin() const noexcept
        {
            return const_iterator(
                first_, first_, last_, front_skip_, back_skip_);
        }

        const_iterator end() const noexcept
        {
            return const_iterator(
                last_, first_, last_, front_skip_, back_skip_);
        }
    };

    slice_impl() noexcept = default; // LCOV_EXCL_LINE defaulted ctor, gcov counts a phantom line

    explicit slice_impl(BufferSequence const& bs) noexcept
        : first_(capy::begin(bs))
        , last_(capy::end(bs))
    {
    }

    slice_impl(
        BufferSequence const& bs,
        std::size_t offset,
        std::size_t length) noexcept
    {
        auto it_begin = capy::begin(bs);
        auto it_end = capy::end(bs);

        std::size_t total = 0;
        for (auto it = it_begin; it != it_end; ++it)
            total += (*it).size();

        if (offset > total)
            offset = total;
        std::size_t const remaining = total - offset;
        if (length > remaining)
            length = remaining;

        first_ = it_begin;
        last_ = it_end;

        std::size_t skip = offset;
        while (first_ != last_)
        {
            std::size_t const buf_size = (*first_).size();
            if (skip < buf_size)
            {
                front_skip_ = skip;
                break;
            }
            skip -= buf_size;
            ++first_;
        }

        std::size_t left = length;
        auto cursor = first_;
        std::size_t cursor_front = front_skip_;
        while (cursor != last_ && left > 0)
        {
            std::size_t const buf_size = (*cursor).size();
            std::size_t const avail = buf_size - cursor_front;
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

    data_view data() const noexcept
    {
        return data_view(first_, last_, front_skip_, back_skip_);
    }

    void remove_prefix(std::size_t n) noexcept
    {
        while (n > 0 && first_ != last_)
        {
            std::size_t const buf_total = (*first_).size();
            std::size_t live = buf_total - front_skip_;
            auto next = first_;
            ++next;
            bool const is_last = (next == last_);
            if (is_last)
                live -= back_skip_;

            if (n < live)
            {
                front_skip_ += n;
                return;
            }

            n -= live;
            if (is_last)
            {
                first_ = last_;
                front_skip_ = 0;
                back_skip_ = 0;
                return;
            }
            ++first_;
            front_skip_ = 0;
        }
    }
};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
