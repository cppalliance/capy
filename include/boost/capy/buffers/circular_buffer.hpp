//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_CIRCULAR_BUFFER_HPP
#define BOOST_CAPY_BUFFERS_CIRCULAR_BUFFER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/buffers/detail/except.hpp>

namespace boost {
namespace capy {
namespace buffers {

/** A circular buffer.

    This implements a fixed-size, circular
    buffer. Buffer sequences returned from
    @ref prepare and @ref data always have
    length two.
*/
class circular_buffer
{
    unsigned char* base_ = nullptr;
    std::size_t cap_ = 0;
    std::size_t in_pos_ = 0;
    std::size_t in_len_ = 0;
    std::size_t out_size_ = 0;

public:
    /** The ConstBufferSequence used to
        represent the readable bytes.
    */
    using const_buffers_type =
        const_buffer_pair;

    /** The MutableBufferSequence used to
        represent the writable bytes.
    */
    using mutable_buffers_type =
        mutable_buffer_pair;

    /** Constructor.
    */
    circular_buffer() = default;

    /** Constructor.
    */
    circular_buffer(
        circular_buffer const&) = default;

    /** Constructor.
    */
    circular_buffer(
        void* base,
        std::size_t capacity) noexcept
        : base_(static_cast<
            unsigned char*>(base))
        , cap_(capacity)
    {
    }

    /** Constructor.
    */
    circular_buffer(
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

    /** Assignment.
    */
    circular_buffer& operator=(
        circular_buffer const&) = default;

    /** Returns the number of readable bytes.
    */
    std::size_t
    size() const noexcept
    {
        return in_len_;
    }

    /** Returns the maximum sum of the input and
        output sequence sizes.
    */
    std::size_t
    max_size() const noexcept
    {
        return cap_;
    }

    /** Returns the number of writable bytes.
    */
    std::size_t
    capacity() const noexcept
    {
        return cap_ - in_len_;
    }

    /** Returns a constant buffer sequence representing
        the readable bytes.
    */
    BOOST_CAPY_DECL
    const_buffers_type
    data() const noexcept;

    /** Returns a mutable buffer sequence representing
        the writable bytes.

        All buffers sequences previously
        obtained using @ref prepare become
        invalid.

        @param n The desired number of bytes in
        the returned buffer sequence.

        @throw std::length_error if @ref size() + n
        exceeds @ref max_size().
    */
    BOOST_CAPY_DECL
    mutable_buffers_type
    prepare(std::size_t n);

    /** Append writable bytes to the readable bytes.

        Appends n bytes from the start of the
        writable bytes to the end of the
        readable bytes. The remainder of the
        writable bytes are discarded. If n is
        greater than the number of writable
        bytes, all writable bytes are appended
        to the readable bytes.

        All buffer sequences previously obtained
        using @ref prepare are invalidated.

        Buffer sequences previously obtained
        using @ref data remain valid.

        @param n The number of bytes to append. If
        this number is greater than the number
        of writable bytes, all writable bytes
        are appended.
    */
    BOOST_CAPY_DECL
    void
    commit(std::size_t n) noexcept;

    /** Remove bytes from beginning of the readable bytes.

        All buffers sequences previously
        obtained using @ref data are
        invalidated.

        Buffer sequences previously obtained
        using @ref prepare remain valid.

        @param n The number of bytes to remove.
        If this number is greater than the
        number of readable bytes, all readable
        bytes are removed.
    */
    BOOST_CAPY_DECL
    void
    consume(std::size_t n) noexcept;
};

} // buffers
} // capy
} // boost

#endif
