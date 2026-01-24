//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_HPP
#define BOOST_CAPY_BUFFERS_HPP

#include <boost/capy/detail/config.hpp>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <type_traits>

// https://www.boost.org/doc/libs/1_65_0/doc/html/boost_asio/reference/ConstBufferSequence.html

namespace boost {

namespace asio {
class const_buffer;
class mutable_buffer;
} // asio

namespace capy {

class const_buffer;
class mutable_buffer;

namespace detail {

// satisfies Asio's buffer constructors, CANNOT be removed!
template<class T, std::size_t Extent = (std::size_t)(-1)>
class basic_buffer
{
    constexpr auto data() const noexcept ->
        std::conditional_t<std::is_const_v<T>, void const*, void*>
    {
        return p_;
    }

    constexpr std::size_t size() const noexcept
    {
        return n_;
    }

    friend class capy::const_buffer;
    friend class capy::mutable_buffer;
    friend class asio::const_buffer;
    friend class asio::mutable_buffer;
    basic_buffer() = default;
    constexpr basic_buffer(T* p, std::size_t n) noexcept : p_(p), n_(n) {}
    constexpr basic_buffer<T, (std::size_t)(-1)> subspan(
        std::size_t, std::size_t = (std::size_t)(-1)) const noexcept;

    T* p_ = nullptr;
    std::size_t n_ = 0;
};

} // detail

//------------------------------------------------

/** size tag for `tag_invoke`

    This type is used in overloads of `tag_invoke`
    for user-defined types to customize the `size()`
    algorithm.
*/
struct size_tag {};

/** slice tag for `tag_invoke`

    This type is used in overloads of `tag_invoke`
    for user-defined types to customize the slicing
    algorithms.
*/
struct slice_tag {};

/** slice constants for slice customization

    This defines the possible values passed to
    overloads of `tag_invoke` for user-defined
    types which customize the slicing algorithms.
*/
enum class slice_how
{
    /// Indicates that the front of the buffer sequence should be trimmed
    remove_prefix,

    /// Indicates that the front of the buffer sequence should be preserved
    keep_prefix
};

//------------------------------------------------

/** Holds a contiguous range of modifiable bytes
*/
class mutable_buffer
    : public detail::basic_buffer<unsigned char>
{
public:
    /** Constructor.
    */
    mutable_buffer() = default;

    /** Constructor.
    */
    mutable_buffer(
        mutable_buffer const&) = default;

    /** Assignment.
    */
    mutable_buffer& operator=(
        mutable_buffer const&) = default;

    /** Constructor.
    */
    constexpr mutable_buffer(
        void* data, std::size_t size) noexcept
        : basic_buffer<unsigned char>(
            static_cast<unsigned char*>(data), size)
    {
    }

    /** Constructor
    */
    template<class MutableBuffer>
        requires std::same_as<MutableBuffer, asio::mutable_buffer>
    constexpr mutable_buffer(
        MutableBuffer const& b) noexcept
        : basic_buffer<unsigned char>(
            static_cast<unsigned char*>(
                b.data()), b.size())
    {
    }

    /** Return a pointer to the beginning of the memory region
    */
    constexpr void* data() const noexcept
    {
        return p_;
    }

    /** Return the number of valid bytes in the referenced memory region
    */
    constexpr std::size_t size() const noexcept
    {
        return n_;
    }

    /** Remove a prefix of the memory region

        If the requested number of bytes is larger than the current size,
        the resulting buffer will have size 0.

        @param n The number of bytes to remove.
    */
    mutable_buffer&
    operator+=(std::size_t n) noexcept
    {
        if( n > n_)
            n = n_;
        p_ += n;
        n_ -= n;
        return *this;
    }

    /** Remove a slice from the buffer
    */
    friend
    void
    tag_invoke(
        slice_tag const&,
        mutable_buffer& b,
        slice_how how,
        std::size_t n) noexcept
    {
        b.do_slice(how, n);
    }

private:
    void do_slice(
        slice_how how, std::size_t n) noexcept
    {
        switch(how)
        {
        case slice_how::remove_prefix:
            *this += n;
            return;

        case slice_how::keep_prefix:
            if( n < n_)
                n_ = n;
            return;
        }
    }
};

//------------------------------------------------

/** Holds a contiguous range of unmodifiable bytes
*/
class const_buffer
    : public detail::basic_buffer<unsigned char const>
{
public:
    /** Constructor
    */
    const_buffer() = default;

    /** Constructor
    */
    const_buffer(const_buffer const&) = default;

    /** Assignment

        @par Postconditions
        @code
        this->data() == other.data() && this->size() == other.size()
        @endcode
    */
    const_buffer& operator=(
        const_buffer const& other) = default;

    /** Constructor
    */
    constexpr const_buffer(
        void const* data, std::size_t size) noexcept
        : basic_buffer<unsigned char const>(
            static_cast<unsigned char const*>(data), size)
    {
    }

    /** Constructor
    */
    constexpr const_buffer(
        mutable_buffer const& b) noexcept
        : basic_buffer<unsigned char const>(
            static_cast<unsigned char const*>(b.data()), b.size())
    {
    }

    /** Constructor
    */
    template<class ConstBuffer>
        requires (std::same_as<ConstBuffer, asio::const_buffer> ||
                  std::same_as<ConstBuffer, asio::mutable_buffer>)
    constexpr const_buffer(
        ConstBuffer const& b) noexcept
        : basic_buffer<unsigned char const>(
            static_cast<unsigned char const*>(
                b.data()), b.size())
    {
    }

    /** Return a pointer to the beginning of the memory region
    */
    constexpr void const* data() const noexcept
    {
        return p_;
    }

    /** Return the number of valid bytes in the referenced memory region
    */
    constexpr std::size_t size() const noexcept
    {
        return n_;
    }

    /** Remove a prefix of the memory region

        If the requested number of bytes is larger than the current size,
        the resulting buffer will have size 0.

        @param n The number of bytes to remove.
    */
    const_buffer&
    operator+=(std::size_t n) noexcept
    {
        if( n > n_)
            n = n_;
        p_ += n;
        n_ -= n;
        return *this;
    }

    /** Remove a slice from the buffer
    */
    friend
    void
    tag_invoke(
        slice_tag const&,
        const_buffer& b,
        slice_how how,
        std::size_t n) noexcept
    {
        b.do_slice(how, n);
    }

private:
    void do_slice(
        slice_how how, std::size_t n) noexcept
    {
        switch(how)
        {
        case slice_how::remove_prefix:
            *this += n;
            return;

        case slice_how::keep_prefix:
            if( n < n_)
                n_ = n;
            return;
        }
    }
};

//------------------------------------------------

/** Concept for types that model ConstBufferSequence.

    A type satisfies `ConstBufferSequence` if it is convertible
    to `const_buffer`, or if it is a bidirectional range whose
    value type is convertible to `const_buffer`.
*/
template<typename T>
concept ConstBufferSequence =
    std::is_convertible_v<T, const_buffer> || (
        std::ranges::bidirectional_range<T> &&
        std::is_convertible_v<std::ranges::range_value_t<T>, const_buffer>);

/** Concept for types that model MutableBufferSequence.

    A type satisfies `MutableBufferSequence` if it is convertible
    to `mutable_buffer`, or if it is a bidirectional range whose
    value type is convertible to `mutable_buffer`.
*/
template<typename T>
concept MutableBufferSequence =
    std::is_convertible_v<T, mutable_buffer> || (
        std::ranges::bidirectional_range<T> &&
        std::is_convertible_v<std::ranges::range_value_t<T>, mutable_buffer>);

//------------------------------------------------------------------------------

/** Return an iterator pointing to the first element of a buffer sequence

    This function returns an iterator to the beginning of the range denoted by
    `t`. It handles both ranges and single buffers uniformly.

    @par Constraints
    @code
    const_buffer_sequence<T>
    @endcode

    @param t The buffer sequence
*/
constexpr struct begin_mrdocs_workaround_t
{
    template<std::convertible_to<const_buffer> ConvertibleToBuffer>
    auto operator()(ConvertibleToBuffer const& b) const noexcept -> ConvertibleToBuffer const*
    {
        return std::addressof(b);
    }

    template<ConstBufferSequence BS>
        requires (!std::convertible_to<BS, const_buffer>)
    auto operator()(BS const& bs) const noexcept
    {
        return std::ranges::begin(bs);
    }

    template<ConstBufferSequence BS>
        requires (!std::convertible_to<BS, const_buffer>)
    auto operator()(BS& bs) const noexcept
    {
        return std::ranges::begin(bs);
    }
} begin {};

//------------------------------------------------------------------------------

/** Return an iterator to the end of the buffer sequence

    This function returns an iterator to the end of the range denoted by
    `t`. It handles both ranges and single buffers uniformly.

    @par Constraints
    @code
    const_buffer_sequence<T>
    @endcode

    @param t The buffer sequence
*/
constexpr struct end_mrdocs_workaround_t
{
    template<std::convertible_to<const_buffer> ConvertibleToBuffer>
    auto operator()(ConvertibleToBuffer const& b) const noexcept -> ConvertibleToBuffer const*
    {
        return std::addressof(b) + 1;
    }

    template<ConstBufferSequence BS>
        requires (!std::convertible_to<BS, const_buffer>)
    auto operator()(BS const& bs) const noexcept
    {
        return std::ranges::end(bs);
    }

    template<ConstBufferSequence BS>
        requires (!std::convertible_to<BS, const_buffer>)
    auto operator()(BS& bs) const noexcept
    {
        return std::ranges::end(bs);
    }
} end {};

//------------------------------------------------------------------------------

template<ConstBufferSequence CB>
std::size_t
tag_invoke(
    size_tag const&,
    CB const& bs) noexcept
{
    std::size_t n = 0;
    auto const e = end(bs);
    for(auto it = begin(bs); it != e; ++it)
        n += const_buffer(*it).size();
    return n;
}

//------------------------------------------------------------------------------

/** Return the total number of bytes in a buffer sequence

    This function returns the sum of the number of bytes in each contiguous
    buffer contained in the range or value. This is different from the length
    of the sequence returned by `std::ranges::size(t)`

    @par Constraints
    @code
    ConstBufferSequence<T>
    @endcode

    @par Example
    @code
    template<ConstBufferSequence CB>
    bool is_small( CB const& bs ) noexcept
    {
        return buffer_size(bs) < 100;
    }
    @endcode
*/
constexpr struct buffer_size_mrdocs_workaround_t
{
    template<ConstBufferSequence CB>
    constexpr std::size_t operator()(
        CB const& bs) const noexcept
    {
        return tag_invoke(size_tag{}, bs);
    }
} buffer_size {};

//-----------------------------------------------

/** Check if a buffer sequence contains no data.

    A buffer sequence is considered empty if all of its
    constituent buffers have size zero, or if the sequence
    contains no buffers.

    @param bs The buffer sequence to check.

    @return `true` if the total size of all buffers is zero.
*/
constexpr struct buffer_empty_mrdocs_workaround_t
{
    template<ConstBufferSequence CB>
    constexpr bool operator()(
        CB const& bs) const noexcept
    {
        auto it = begin(bs);
        auto const end_ = end(bs);
        while(it != end_)
        {
            const_buffer b(*it++);
            if(b.size() != 0)
                return false;
        }
        return true;
    }
} buffer_empty {};

//-----------------------------------------------

namespace detail {

template<class It>
auto
length_impl(It first, It last, int)
    -> decltype(static_cast<std::size_t>(last - first))
{
    return static_cast<std::size_t>(last - first);
}

template<class It>
std::size_t
length_impl(It first, It last, long)
{
    std::size_t n = 0;
    while(first != last)
    {
        ++first;
        ++n;
    }
    return n;
}

} // detail

/** Return the number of elements in a buffer sequence.
*/
template<ConstBufferSequence CB>
std::size_t
buffer_length(CB const& bs)
{
    return detail::length_impl(
        begin(bs), end(bs), 0);
}

/** Alias for const_buffer or mutable_buffer depending on sequence type.
*/
template<typename BS>
using buffer_type = std::conditional_t<
    MutableBufferSequence<BS>,
    mutable_buffer, const_buffer>;

} // capy
} // boost

#endif
