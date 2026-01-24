//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_VECTOR_DYNAMIC_BUFFER_HPP
#define BOOST_CAPY_BUFFERS_VECTOR_DYNAMIC_BUFFER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/detail/except.hpp>
#include <type_traits>
#include <vector>

namespace boost {
namespace capy {

/** A dynamic buffer using an underlying vector.

    This class adapts a `std::vector` of byte-sized elements
    to satisfy the DynamicBuffer concept. The vector provides
    automatic memory management and growth.

    @par Constraints

    The element type `T` must be a fundamental type with
    `sizeof( T ) == 1`. This includes `char`, `unsigned char`,
    `signed char`, and similar byte-sized fundamental types.

    @par Example
    @code
    std::vector<unsigned char> v;
    vector_dynamic_buffer vb( &v );

    // Write data
    auto mb = vb.prepare( 100 );
    std::memcpy( mb.data(), "hello", 5 );
    vb.commit( 5 );

    // Read data
    auto data = vb.data();
    // process data...
    vb.consume( 5 );
    @endcode

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Unsafe.

    @tparam T The element type. Must be fundamental with sizeof 1.
    @tparam Allocator The allocator type for the vector.

    @see flat_dynamic_buffer, circular_dynamic_buffer, string_dynamic_buffer
*/
template<
    class T,
    class Allocator = std::allocator<T>>
    requires std::is_fundamental_v<T> && (sizeof(T) == 1)
class basic_vector_dynamic_buffer
{
    std::vector<T, Allocator>* v_;
    std::size_t max_size_;

    std::size_t in_size_ = 0;
    std::size_t out_size_ = 0;

public:
    /// Indicates this is a DynamicBuffer adapter over external storage.
    using is_dynamic_buffer_adapter = void;

    /// The underlying vector type.
    using vector_type = std::vector<T, Allocator>;

    /// The ConstBufferSequence type for readable bytes.
    using const_buffers_type = const_buffer;

    /// The MutableBufferSequence type for writable bytes.
    using mutable_buffers_type = mutable_buffer;

    ~basic_vector_dynamic_buffer() = default;

    /** Move constructor.
    */
    basic_vector_dynamic_buffer(
        basic_vector_dynamic_buffer&& other) noexcept
        : v_(other.v_)
        , max_size_(other.max_size_)
        , in_size_(other.in_size_)
        , out_size_(other.out_size_)
    {
        other.v_ = nullptr;
    }

    /** Construct a dynamic buffer over a vector.

        @param v Pointer to the vector to use as storage.
        @param max_size Optional maximum size limit. Defaults
            to the vector's `max_size()`.
    */
    explicit
    basic_vector_dynamic_buffer(
        vector_type* v,
        std::size_t max_size =
            std::size_t(-1)) noexcept
        : v_(v)
        , max_size_(
            max_size > v_->max_size()
                ? v_->max_size()
                : max_size)
    {
        if(v_->size() > max_size_)
            v_->resize(max_size_);
        in_size_ = v_->size();
    }

    /// Copy assignment is deleted.
    basic_vector_dynamic_buffer& operator=(
        basic_vector_dynamic_buffer const&) = delete;

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
        return max_size_;
    }

    /// Return the number of writable bytes without reallocation.
    std::size_t
    capacity() const noexcept
    {
        if(v_->capacity() <= max_size_)
            return v_->capacity() - in_size_;
        return max_size_ - in_size_;
    }

    /// Return a buffer sequence representing the readable bytes.
    const_buffers_type
    data() const noexcept
    {
        return const_buffers_type(
            v_->data(), in_size_);
    }

    /** Return a buffer sequence for writing.

        Invalidates buffer sequences previously obtained
        from @ref prepare.

        @param n The desired number of writable bytes.

        @return A mutable buffer sequence of size @p n.

        @throws std::invalid_argument if `size() + n > max_size()`.
    */
    mutable_buffers_type
    prepare(std::size_t n)
    {
        if(n > max_size_ - in_size_)
            detail::throw_invalid_argument();

        if(v_->size() < in_size_ + n)
            v_->resize(in_size_ + n);
        out_size_ = n;
        return mutable_buffers_type(
            v_->data() + in_size_, out_size_);
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
    commit(std::size_t n) noexcept
    {
        if(n < out_size_)
            in_size_ += n;
        else
            in_size_ += out_size_;
        out_size_ = 0;
        v_->resize(in_size_);
    }

    /** Remove bytes from the beginning of the input sequence.

        Invalidates buffer sequences previously obtained
        from @ref data. Buffer sequences from @ref prepare
        remain valid.

        @param n The number of bytes to consume. If greater
            than @ref size(), all readable bytes are consumed.
    */
    void
    consume(std::size_t n) noexcept
    {
        if(n < in_size_)
        {
            v_->erase(v_->begin(), v_->begin() + n);
            in_size_ -= n;
        }
        else
        {
            v_->clear();
            in_size_ = 0;
        }
        out_size_ = 0;
    }
};

/// A dynamic buffer using `std::vector<unsigned char>`.
using vector_dynamic_buffer =
    basic_vector_dynamic_buffer<unsigned char>;

} // capy
} // boost

#endif
