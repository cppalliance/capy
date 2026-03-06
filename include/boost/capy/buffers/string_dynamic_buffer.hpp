//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_STRING_DYNAMIC_BUFFER_HPP
#define BOOST_CAPY_BUFFERS_STRING_DYNAMIC_BUFFER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/detail/except.hpp>
#include <string>

namespace boost {
namespace capy {

/** A dynamic buffer backed by a `std::basic_string`.

    This adapter wraps an externally-owned string and
    exposes it through the @ref DynamicBuffer interface.
    Readable bytes occupy the front of the string; writable
    bytes are appended by `prepare` and made readable by
    `commit`.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Unsafe.

    @tparam CharT The character type.
    @tparam Traits The character traits type.
    @tparam Allocator The allocator type.

    @see DynamicBuffer, string_dynamic_buffer, dynamic_buffer
*/
template<
    class CharT,
    class Traits = std::char_traits<CharT>,
    class Allocator = std::allocator<CharT>>
class basic_string_dynamic_buffer
{
    std::basic_string<
        CharT, Traits, Allocator>* s_;
    std::size_t max_size_;

    std::size_t in_size_ = 0;
    std::size_t out_size_ = 0;

public:
    /// Indicates this is a DynamicBuffer adapter over external storage.
    using is_dynamic_buffer_adapter = void;

    /// The underlying string type.
    using string_type = std::basic_string<
        CharT, Traits, Allocator>;

    /// The ConstBufferSequence type for readable bytes.
    using const_buffers_type = const_buffer;

    /// The MutableBufferSequence type for writable bytes.
    using mutable_buffers_type = mutable_buffer;

    /// Destroy the buffer.
    ~basic_string_dynamic_buffer() = default;

    /// Construct by moving from another buffer.
    basic_string_dynamic_buffer(
        basic_string_dynamic_buffer&& other) noexcept
        : s_(other.s_)
        , max_size_(other.max_size_)
        , in_size_(other.in_size_)
        , out_size_(other.out_size_)
    {
        other.s_ = nullptr;
    }

    /** Construct from an existing string.

        @param s Pointer to the string to wrap. Must
            remain valid for the lifetime of this object.
        @param max_size Optional upper bound on the number
            of bytes the buffer may hold.
    */
    explicit
    basic_string_dynamic_buffer(
        string_type* s,
        std::size_t max_size =
            std::size_t(-1)) noexcept
        : s_(s)
        , max_size_(
            max_size > s_->max_size()
                ? s_->max_size()
                : max_size)
    {
        if(s_->size() > max_size_)
            s_->resize(max_size_);
        in_size_ = s_->size();
    }

    /// Copy assignment is deleted.
    basic_string_dynamic_buffer& operator=(
        basic_string_dynamic_buffer const&) = delete;

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
        if(s_->capacity() <= max_size_)
            return s_->capacity() - in_size_;
        return max_size_ - in_size_;
    }

    /// Return a buffer sequence representing the readable bytes.
    const_buffers_type
    data() const noexcept
    {
        return const_buffers_type(
            s_->data(), in_size_);
    }

    /** Prepare writable space of at least `n` bytes.

        Invalidates iterators and references returned by
        previous calls to `data` and `prepare`.

        @throws std::invalid_argument if `n` exceeds
            available space.

        @param n The number of bytes to prepare.

        @return A mutable buffer of exactly `n` bytes.
    */
    mutable_buffers_type
    prepare(std::size_t n)
    {
        // n exceeds available space
        if(n > max_size_ - in_size_)
            detail::throw_invalid_argument();

        if( s_->size() < in_size_ + n)
            s_->resize(in_size_ + n);
        out_size_ = n;
        return mutable_buffers_type(
            &(*s_)[in_size_], out_size_);
    }

    /** Move bytes from the writable to the readable area.

        Invalidates iterators and references returned by
        previous calls to `data` and `prepare`.

        @param n The number of bytes to commit. Clamped
            to the size of the writable area.
    */
    void commit(std::size_t n) noexcept
    {
        if(n < out_size_)
            in_size_ += n;
        else
            in_size_ += out_size_;
        out_size_ = 0;
        s_->resize(in_size_);
    }

    /** Remove bytes from the beginning of the readable area.

        Invalidates iterators and references returned by
        previous calls to `data` and `prepare`.

        @param n The number of bytes to consume. Clamped
            to the number of readable bytes.
    */
    void consume(std::size_t n) noexcept
    {
        if(n < in_size_)
        {
            s_->erase(0, n);
            in_size_ -= n;
        }
        else
        {
            s_->clear();
            in_size_ = 0;
        }
        out_size_ = 0;
    }
};

/// A dynamic buffer using `std::string`.
using string_dynamic_buffer = basic_string_dynamic_buffer<char>;

/** Create a dynamic buffer from a string.

    @param s The string to wrap.
    @param max_size Optional maximum size limit.
    @return A string_dynamic_buffer wrapping the string.
*/
template<class CharT, class Traits, class Allocator>
basic_string_dynamic_buffer<CharT, Traits, Allocator>
dynamic_buffer(
    std::basic_string<CharT, Traits, Allocator>& s,
    std::size_t max_size = std::size_t(-1))
{
    return basic_string_dynamic_buffer<CharT, Traits, Allocator>(&s, max_size);
}

} // capy
} // boost

#endif
