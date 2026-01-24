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
#include <boost/capy/detail/except.hpp>

namespace boost {
namespace capy {

/** A fixed-capacity linear buffer satisfying DynamicBuffer.

    This class provides a contiguous buffer with fixed capacity
    determined at construction. Buffer sequences returned from
    @ref data and @ref prepare always contain exactly one element,
    making it suitable for APIs requiring contiguous memory.

    @par Example
    @code
    char storage[1024];
    flat_buffer fb( storage, sizeof( storage ) );

    // Write data
    auto mb = fb.prepare( 100 );
    std::memcpy( mb.data(), "hello", 5 );
    fb.commit( 5 );

    // Read data
    auto data = fb.data();
    // process data...
    fb.consume( 5 );
    @endcode

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Unsafe.

    @see circular_dynamic_buffer, string_dynamic_buffer
*/
class flat_buffer
{
    unsigned char* data_ = nullptr;
    std::size_t cap_ = 0;
    std::size_t in_pos_ = 0;
    std::size_t in_size_ = 0;
    std::size_t out_size_ = 0;

public:
    /// Indicates this is a DynamicBuffer adapter over external storage.
    using is_dynamic_buffer_adapter = void;

    /// The ConstBufferSequence type for readable bytes.
    using const_buffers_type = const_buffer;

    /// The MutableBufferSequence type for writable bytes.
    using mutable_buffers_type = mutable_buffer;

    /// Construct an empty flat buffer with zero capacity.
    flat_buffer() = default;

    /** Construct a flat buffer over existing storage.

        @param data Pointer to the storage.
        @param capacity Size of the storage in bytes.
        @param initial_size Number of bytes already present as
            readable. Must not exceed @p capacity.

        @throws std::invalid_argument if initial_size > capacity.
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
        if(in_size_ > cap_)
            detail::throw_invalid_argument();
    }

    /// Copy constructor.
    flat_buffer(
        flat_buffer const&) = default;

    /// Copy assignment.
    flat_buffer& operator=(
        flat_buffer const&) = default;

    /// Return the number of readable bytes.
    std::size_t
    size() const noexcept
    {
        return in_size_;
    }

    /// Return the maximum number of bytes the buffer can hold.
    std::size_t
    max_size() const noexcept
    {
        return cap_;
    }

    /// Return the number of writable bytes without reallocation.
    std::size_t
    capacity() const noexcept
    {
        return cap_ - (in_pos_ + in_size_);
    }

    /// Return a buffer sequence representing the readable bytes.
    const_buffers_type
    data() const noexcept
    {
        return const_buffers_type(
            data_ + in_pos_, in_size_);
    }

    /** Return a buffer sequence for writing.

        Invalidates buffer sequences previously obtained
        from @ref prepare.

        @param n The desired number of writable bytes.

        @return A mutable buffer sequence of size @p n.

        @throws std::invalid_argument if `n > capacity()`.
    */
    mutable_buffers_type
    prepare(std::size_t n)
    {
        if( n > capacity() )
            detail::throw_invalid_argument();

        out_size_ = n;
        return mutable_buffers_type(
            data_ + in_pos_ + in_size_, n);
    }

    /** Move bytes from the output to the input sequence.

        Invalidates buffer sequences previously obtained
        from @ref prepare. Buffer sequences from @ref data
        remain valid.

        @param n The number of bytes to commit. If greater
            than the prepared size, all prepared bytes
            are committed.
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

    /** Remove bytes from the beginning of the input sequence.

        Invalidates buffer sequences previously obtained
        from @ref data. Buffer sequences from @ref prepare
        remain valid.

        @param n The number of bytes to consume. If greater
            than @ref size(), all readable bytes are consumed.
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

} // capy
} // boost

#endif
