//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_SOME_BUFFERS_HPP
#define BOOST_CAPY_BUFFERS_SOME_BUFFERS_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>

#include <cstddef>
#include <new>
#include <utility>

namespace boost {
namespace capy {

namespace detail {

template<class Buffer, class BS, std::size_t... Is>
std::size_t
fill_buffers_impl(
    Buffer* arr,
    BS const& bs,
    std::index_sequence<Is...>) noexcept
{
    auto it = begin(bs);
    auto const last = end(bs);
    std::size_t n = 0;
    ((it != last ? (::new(&arr[Is]) Buffer(*it++), ++n, void()) : void()), ...);
    return n;
}

template<class Buffer, class BS>
std::size_t
fill_buffers(Buffer* arr, BS const& bs) noexcept
{
    return fill_buffers_impl(
        arr, bs, std::make_index_sequence<max_iovec_>{});
}

} // detail

//------------------------------------------------

/** A buffer sequence holding up to max_iovec_ const buffers.

    This class stores a fixed-size array of const_buffer
    descriptors, populated from an arbitrary buffer sequence.
    It provides efficient storage for small buffer sequences
    without dynamic allocation.

    @par Usage

    @code
    void process(ConstBufferSequence auto const& buffers)
    {
        some_const_buffers bufs(buffers);
        // use bufs.data(), bufs.size()
    }
    @endcode
*/
class some_const_buffers
{
    union {
        int dummy_;
        const_buffer arr_[detail::max_iovec_];
    };
    std::size_t n_ = 0;

public:
    /** Default constructor.

        Constructs an empty buffer sequence.
    */
    some_const_buffers() noexcept
        : dummy_(0)
    {
    }

    /** Copy constructor.
    */
    some_const_buffers(some_const_buffers const& other) noexcept
        : n_(other.n_)
    {
        for(std::size_t i = 0; i < n_; ++i)
            ::new(&arr_[i]) const_buffer(other.arr_[i]);
    }

    /** Construct from a buffer sequence.

        Copies up to @ref detail::max_iovec_ buffer descriptors
        from the source sequence into the internal array.

        @param bs The buffer sequence to copy from.
    */
    template<ConstBufferSequence BS>
    explicit
    some_const_buffers(BS const& bs) noexcept
        : n_(detail::fill_buffers(arr_, bs))
    {
    }

    /** Destructor.
    */
    ~some_const_buffers()
    {
        while(n_--)
            arr_[n_].~const_buffer();
    }

    /** Return a pointer to the buffer array.
    */
    const_buffer*
    data() noexcept
    {
        return arr_;
    }

    /** Return a pointer to the buffer array.
    */
    const_buffer const*
    data() const noexcept
    {
        return arr_;
    }

    /** Return the number of buffers.
    */
    std::size_t
    size() const noexcept
    {
        return n_;
    }

    /** Return an iterator to the beginning.
    */
    const_buffer*
    begin() noexcept
    {
        return arr_;
    }

    /** Return an iterator to the beginning.
    */
    const_buffer const*
    begin() const noexcept
    {
        return arr_;
    }

    /** Return an iterator to the end.
    */
    const_buffer*
    end() noexcept
    {
        return arr_ + n_;
    }

    /** Return an iterator to the end.
    */
    const_buffer const*
    end() const noexcept
    {
        return arr_ + n_;
    }
};

//------------------------------------------------

/** A buffer sequence holding up to max_iovec_ mutable buffers.

    This class stores a fixed-size array of mutable_buffer
    descriptors, populated from an arbitrary buffer sequence.
    It provides efficient storage for small buffer sequences
    without dynamic allocation.

    @par Usage

    @code
    void process(MutableBufferSequence auto const& buffers)
    {
        some_mutable_buffers bufs(buffers);
        // use bufs.data(), bufs.size()
    }
    @endcode
*/
class some_mutable_buffers
{
    union {
        int dummy_;
        mutable_buffer arr_[detail::max_iovec_];
    };
    std::size_t n_ = 0;

public:
    /** Default constructor.

        Constructs an empty buffer sequence.
    */
    some_mutable_buffers() noexcept
        : dummy_(0)
    {
    }

    /** Copy constructor.
    */
    some_mutable_buffers(some_mutable_buffers const& other) noexcept
        : n_(other.n_)
    {
        for(std::size_t i = 0; i < n_; ++i)
            ::new(&arr_[i]) mutable_buffer(other.arr_[i]);
    }

    /** Construct from a buffer sequence.

        Copies up to @ref detail::max_iovec_ buffer descriptors
        from the source sequence into the internal array.

        @param bs The buffer sequence to copy from.
    */
    template<MutableBufferSequence BS>
    explicit
    some_mutable_buffers(BS const& bs) noexcept
        : n_(detail::fill_buffers(arr_, bs))
    {
    }

    /** Destructor.
    */
    ~some_mutable_buffers()
    {
        while(n_--)
            arr_[n_].~mutable_buffer();
    }

    /** Return a pointer to the buffer array.
    */
    mutable_buffer*
    data() noexcept
    {
        return arr_;
    }

    /** Return a pointer to the buffer array.
    */
    mutable_buffer const*
    data() const noexcept
    {
        return arr_;
    }

    /** Return the number of buffers.
    */
    std::size_t
    size() const noexcept
    {
        return n_;
    }

    /** Return an iterator to the beginning.
    */
    mutable_buffer*
    begin() noexcept
    {
        return arr_;
    }

    /** Return an iterator to the beginning.
    */
    mutable_buffer const*
    begin() const noexcept
    {
        return arr_;
    }

    /** Return an iterator to the end.
    */
    mutable_buffer*
    end() noexcept
    {
        return arr_ + n_;
    }

    /** Return an iterator to the end.
    */
    mutable_buffer const*
    end() const noexcept
    {
        return arr_ + n_;
    }
};

} // namespace capy
} // namespace boost

#endif
