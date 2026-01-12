//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_FLAT_BUFFER_HPP
#define BOOST_CAPY_BUFFERS_FLAT_BUFFER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/detail/except.hpp>

namespace boost {
namespace capy {
namespace buffers {

/** A DynamicBuffer with a fixed capacity.

    Buffer sequences returned by this container
    always have a single element.
*/
class flat_buffer
{
    unsigned char* data_ = nullptr;
    std::size_t cap_ = 0;
    std::size_t in_pos_ = 0;
    std::size_t in_size_ = 0;
    std::size_t out_size_ = 0;

public:
    using const_buffers_type = const_buffer;
    using mutable_buffers_type = mutable_buffer;

    /** Constructor.

        Default constructed objects have zero capacity.
    */
    flat_buffer() = default;

    /** Constructor.

        @param data A pointer to the memory to use for the buffer.
        @param capacity The size of the memory pointed to by @p data.
        @param initial_size The initial size of the readable bytes.
            This must be less than or equal to @p capacity.
    */
    flat_buffer(
        void* data,
        std::size_t capacity,
        std::size_t initial_size = 0)
        : data_(static_cast<
            unsigned char*>(data))
        , cap_(capacity)
        , in_size_(initial_size)
    {
        // initial size too large
        if(in_size_ > cap_)
            detail::throw_invalid_argument();
    }

    /** Constructor.
    */
    flat_buffer(
        flat_buffer const&) = default;

    /** Constructor.
    */
    flat_buffer& operator=(
        flat_buffer const&) = default;

    std::size_t
    size() const noexcept
    {
        return in_size_;
    }

    /** Returns the maximum size of the buffer.
    */
    std::size_t
    max_size() const noexcept
    {
        return cap_;
    }

    /** Returns the total number of writable bytes.
    */
    std::size_t
    capacity() const noexcept
    {
        return cap_ - (in_pos_ + in_size_);
    }

    /** Returns a constant buffer sequence representing the readable bytes.
    */
    const_buffers_type
    data() const noexcept
    {
        return const_buffers_type(
            data_ + in_pos_, in_size_);
    }

    /** Returns a mutable buffer sequence representing the writable bytes.
        All buffer sequences previously obtained
        using @ref prepare become invalid.
        @param n The desired number of bytes in the
        returned buffer sequence.
    */
    mutable_buffers_type
    prepare(std::size_t n)
    {
        // n exceeds available space
        if( n > capacity() )
            detail::throw_invalid_argument();

        out_size_ = n;
        return mutable_buffers_type(
            data_ + in_pos_ + in_size_, n);
    }

    /** Commit bytes to the input sequence.
        @param n The number of bytes to commit.
    */
    void
    commit(
        std::size_t n) noexcept
    {
        if(n < out_size_)
            in_size_ += n;
        else
            in_size_ += out_size_;
        out_size_ = 0;
    }

    /** Consume bytes from the input sequence.
        @param n The number of bytes to consume.
    */
    void
    consume(
        std::size_t n) noexcept
    {
        if(n < in_size_)
        {
            in_pos_ += n;
            in_size_ -= n;
        }
        else
        {
            in_pos_ = 0;
            in_size_ = 0;
        }
    }
};

} // buffers
} // capy
} // boost

#endif
