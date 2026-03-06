//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_BUFFER_ARRAY_HPP
#define BOOST_CAPY_BUFFERS_BUFFER_ARRAY_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/except.hpp>
#include <boost/capy/buffers.hpp>

#include <cstddef>
#include <new>
#include <span>
#include <utility>

namespace boost {
namespace capy {

namespace detail {

BOOST_CAPY_DECL
void
buffer_array_remove_prefix(
    const_buffer* arr,
    std::size_t* count,
    std::size_t* total_size,
    std::size_t n) noexcept;

BOOST_CAPY_DECL
void
buffer_array_remove_prefix(
    mutable_buffer* arr,
    std::size_t* count,
    std::size_t* total_size,
    std::size_t n) noexcept;

BOOST_CAPY_DECL
void
buffer_array_keep_prefix(
    const_buffer* arr,
    std::size_t* count,
    std::size_t* total_size,
    std::size_t n) noexcept;

BOOST_CAPY_DECL
void
buffer_array_keep_prefix(
    mutable_buffer* arr,
    std::size_t* count,
    std::size_t* total_size,
    std::size_t n) noexcept;

} // namespace detail

/** A buffer sequence holding up to N buffers.

    This class template stores a fixed-capacity array of buffer
    descriptors, where the actual count can vary from 0 to N.
    It provides efficient storage for small buffer sequences
    without dynamic allocation.

    @par Example
    @code
    void process(ConstBufferSequence auto const& buffers)
    {
        const_buffer_array<4> bufs(buffers);
        // use bufs.begin(), bufs.end(), bufs.to_span()
    }
    @endcode

    @tparam N Maximum number of buffers the array can hold.
    @tparam IsConst If true, holds const_buffer; otherwise mutable_buffer.
*/
template<std::size_t N, bool IsConst>
class buffer_array
{
public:
    /** The type of buffer stored in the array.
    */
    using value_type = std::conditional_t<IsConst, const_buffer, mutable_buffer>;

private:
    std::size_t n_ = 0;
    std::size_t size_ = 0;
    union {
        int dummy_;
        value_type arr_[N];
    };

public:
    /** Construct a default instance.

        Constructs an empty buffer array.
    */
    buffer_array() noexcept
        : dummy_(0)
    {
    }

    /** Construct a copy.
    */
    buffer_array(buffer_array const& other) noexcept
        : n_(other.n_)
        , size_(other.size_)
    {
        for(std::size_t i = 0; i < n_; ++i)
            ::new(&arr_[i]) value_type(other.arr_[i]);
    }

    /** Construct from a single buffer.

        @param b The buffer to store.
    */
    buffer_array(value_type const& b) noexcept
        : dummy_(0)
    {
        if(b.size() != 0)
        {
            ::new(&arr_[0]) value_type(b);
            n_ = 1;
            size_ = b.size();
        }
    }

    /** Construct from a buffer sequence.

        Copies up to N buffer descriptors from the source
        sequence into the internal array. If the sequence
        contains more than N non-empty buffers, excess
        buffers are silently ignored.

        @param bs The buffer sequence to copy from.
    */
    template<class BS>
        requires (IsConst ? ConstBufferSequence<BS> : MutableBufferSequence<BS>)
            && (!std::same_as<std::remove_cvref_t<BS>, buffer_array>)
            && (!std::same_as<std::remove_cvref_t<BS>, value_type>)
    buffer_array(BS const& bs) noexcept
        : dummy_(0)
    {
        auto it = capy::begin(bs);
        auto const last = capy::end(bs);
        while(it != last && n_ < N)
        {
            value_type b(*it);
            if(b.size() != 0)
            {
                ::new(&arr_[n_++]) value_type(b);
                size_ += b.size();
            }
            ++it;
        }
    }

    /** Construct from a buffer sequence with overflow checking.

        Copies buffer descriptors from the source sequence
        into the internal array.

        @param bs The buffer sequence to copy from.

        @throws std::length_error if the sequence contains
        more than N non-empty buffers.
    */
    template<class BS>
        requires (IsConst ? ConstBufferSequence<BS> : MutableBufferSequence<BS>)
    buffer_array(std::in_place_t, BS const& bs)
        : dummy_(0)
    {
        auto it = capy::begin(bs);
        auto const last = capy::end(bs);
        while(it != last)
        {
            value_type b(*it);
            if(b.size() != 0)
            {
                if(n_ >= N)
                    detail::throw_length_error();
                ::new(&arr_[n_++]) value_type(b);
                size_ += b.size();
            }
            ++it;
        }
    }

    /** Construct from an iterator range.

        Copies up to N non-empty buffer descriptors from the
        range `[first, last)`. If the range contains more than
        N non-empty buffers, excess buffers are silently ignored.

        @param first Iterator to the first buffer descriptor.
        @param last Iterator past the last buffer descriptor.
    */
    template<class Iterator>
    buffer_array(Iterator first, Iterator last) noexcept
        : dummy_(0)
    {
        while(first != last && n_ < N)
        {
            value_type b(*first);
            if(b.size() != 0)
            {
                ::new(&arr_[n_++]) value_type(b);
                size_ += b.size();
            }
            ++first;
        }
    }

    /** Construct from an iterator range with overflow checking.

        Copies all non-empty buffer descriptors from the range
        `[first, last)` into the internal array.

        @param first Iterator to the first buffer descriptor.
        @param last Iterator past the last buffer descriptor.

        @throws std::length_error if the range contains more
        than N non-empty buffers.
    */
    template<class Iterator>
    buffer_array(std::in_place_t, Iterator first, Iterator last)
        : dummy_(0)
    {
        while(first != last)
        {
            value_type b(*first);
            if(b.size() != 0)
            {
                if(n_ >= N)
                    detail::throw_length_error();
                ::new(&arr_[n_++]) value_type(b);
                size_ += b.size();
            }
            ++first;
        }
    }

    /** Destructor.
    */
    ~buffer_array()
    {
        while(n_--)
            arr_[n_].~value_type();
    }

    /** Assign by copying.
    */
    buffer_array&
    operator=(buffer_array const& other) noexcept
    {
        if(this != &other)
        {
            while(n_--)
                arr_[n_].~value_type();
            n_ = other.n_;
            size_ = other.size_;
            for(std::size_t i = 0; i < n_; ++i)
                ::new(&arr_[i]) value_type(other.arr_[i]);
        }
        return *this;
    }

    /** Return an iterator to the beginning.
    */
    value_type*
    begin() noexcept
    {
        return arr_;
    }

    /** Return an iterator to the beginning.
    */
    value_type const*
    begin() const noexcept
    {
        return arr_;
    }

    /** Return an iterator to the end.
    */
    value_type*
    end() noexcept
    {
        return arr_ + n_;
    }

    /** Return an iterator to the end.
    */
    value_type const*
    end() const noexcept
    {
        return arr_ + n_;
    }

    /** Return a span of the buffers.
    */
    std::span<value_type>
    to_span() noexcept
    {
        return { arr_, n_ };
    }

    /** Return a span of the buffers.
    */
    std::span<value_type const>
    to_span() const noexcept
    {
        return { arr_, n_ };
    }

    /** Conversion to mutable span.
    */
    operator std::span<value_type>() noexcept
    {
        return { arr_, n_ };
    }

    /** Conversion to const span.
    */
    operator std::span<value_type const>() const noexcept
    {
        return { arr_, n_ };
    }

    /** Return the total byte count in O(1).
    */
    friend
    std::size_t
    tag_invoke(
        size_tag const&,
        buffer_array const& ba) noexcept
    {
        return ba.size_;
    }

    /** Slice customization point.
    */
    friend
    void
    tag_invoke(
        slice_tag const&,
        buffer_array& ba,
        slice_how how,
        std::size_t n) noexcept
    {
        ba.slice_impl(how, n);
    }

private:
    void
    slice_impl(
        slice_how how,
        std::size_t n) noexcept
    {
        switch(how)
        {
        case slice_how::remove_prefix:
            remove_prefix_impl(n);
            break;

        case slice_how::keep_prefix:
            keep_prefix_impl(n);
            break;
        }
    }

    void
    remove_prefix_impl(std::size_t n) noexcept
    {
        detail::buffer_array_remove_prefix(arr_, &n_, &size_, n);
    }

    void
    keep_prefix_impl(std::size_t n) noexcept
    {
        detail::buffer_array_keep_prefix(arr_, &n_, &size_, n);
    }
};

//------------------------------------------------

/** Alias for buffer_array holding const_buffer.

    @tparam N Maximum number of buffers.
*/
template<std::size_t N>
using const_buffer_array = buffer_array<N, true>;

/** Alias for buffer_array holding mutable_buffer.

    @tparam N Maximum number of buffers.
*/
template<std::size_t N>
using mutable_buffer_array = buffer_array<N, false>;

} // namespace capy
} // namespace boost

#endif
