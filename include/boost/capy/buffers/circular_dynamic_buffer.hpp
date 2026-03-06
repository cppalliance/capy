//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_CIRCULAR_DYNAMIC_BUFFER_HPP
#define BOOST_CAPY_BUFFERS_CIRCULAR_DYNAMIC_BUFFER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/detail/except.hpp>

namespace boost {
namespace capy {

/** A fixed-capacity circular buffer satisfying DynamicBuffer.

    This class implements a circular ( ring ) buffer with
    fixed capacity determined at construction. Unlike linear
    buffers, data can wrap around from the end to the beginning,
    enabling efficient FIFO operations without memory copies.

    Buffer sequences returned from @ref data and @ref prepare
    may contain up to two elements to represent wrapped regions.

    @par Example
    @code
    char storage[1024];
    circular_dynamic_buffer cb( storage, sizeof( storage ) );

    // Write data
    auto mb = cb.prepare( 100 );
    std::memcpy( mb.data(), "hello", 5 );
    cb.commit( 5 );

    // Read data
    auto cb_data = cb.data();
    // process cb_data...
    cb.consume( 5 );
    @endcode

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Unsafe.

    @see flat_dynamic_buffer, string_dynamic_buffer
*/
class circular_dynamic_buffer
{
    unsigned char* base_ = nullptr;
    std::size_t cap_ = 0;
    std::size_t in_pos_ = 0;
    std::size_t in_len_ = 0;
    std::size_t out_size_ = 0;

public:
    /// Indicates this is a DynamicBuffer adapter over external storage.
    using is_dynamic_buffer_adapter = void;

    /// The ConstBufferSequence type for readable bytes.
    using const_buffers_type = const_buffer_pair;

    /// The MutableBufferSequence type for writable bytes.
    using mutable_buffers_type = mutable_buffer_pair;

    /// Construct an empty circular buffer with zero capacity.
    circular_dynamic_buffer() = default;

    /// Construct a copy.
    circular_dynamic_buffer(
        circular_dynamic_buffer const&) = default;

    /** Construct a circular buffer over existing storage.

        @param base Pointer to the storage.
        @param capacity Size of the storage in bytes.
    */
    circular_dynamic_buffer(
        void* base,
        std::size_t capacity) noexcept
        : base_(static_cast<
            unsigned char*>(base))
        , cap_(capacity)
    {
    }

    /** Construct a circular buffer with initial readable bytes.

        @param base Pointer to the storage.
        @param capacity Size of the storage in bytes.
        @param initial_size Number of bytes already present as
            readable. Must not exceed @p capacity.

        @throws std::invalid_argument if initial_size > capacity.
    */
    circular_dynamic_buffer(
        void* base,
        std::size_t capacity,
        std::size_t initial_size)
        : base_(static_cast<
            unsigned char*>(base))
        , cap_(capacity)
        , in_len_(initial_size)
    {
        if(in_len_ > capacity)
            detail::throw_invalid_argument();
    }

    /// Assign by copying.
    circular_dynamic_buffer& operator=(
        circular_dynamic_buffer const&) = default;

    /// Return the number of readable bytes.
    std::size_t
    size() const noexcept
    {
        return in_len_;
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
        return cap_ - in_len_;
    }

    /// Return a buffer sequence representing the readable bytes.
    BOOST_CAPY_DECL
    const_buffers_type
    data() const noexcept;

    /** Return a buffer sequence for writing.

        Invalidates buffer sequences previously obtained
        from @ref prepare.

        @param n The desired number of writable bytes.

        @return A mutable buffer sequence of size @p n.

        @throws std::length_error if `size() + n > max_size()`.
    */
    BOOST_CAPY_DECL
    mutable_buffers_type
    prepare(std::size_t n);

    /** Move bytes from the output to the input sequence.

        Invalidates buffer sequences previously obtained
        from @ref prepare. Buffer sequences from @ref data
        remain valid.

        @param n The number of bytes to commit. If greater
            than the prepared size, all prepared bytes
            are committed.
    */
    BOOST_CAPY_DECL
    void
    commit(std::size_t n) noexcept;

    /** Remove bytes from the beginning of the input sequence.

        Invalidates buffer sequences previously obtained
        from @ref data. Buffer sequences from @ref prepare
        remain valid.

        @param n The number of bytes to consume. If greater
            than @ref size(), all readable bytes are consumed.
    */
    BOOST_CAPY_DECL
    void
    consume(std::size_t n) noexcept;
};

} // capy
} // boost

#endif
