//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_MAKE_BUFFER_HPP
#define BOOST_CAPY_BUFFERS_MAKE_BUFFER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <array>
#include <cstdlib>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

BOOST_CAPY_MSVC_WARNING_PUSH
BOOST_CAPY_MSVC_WARNING_DISABLE(4459)

namespace boost {
namespace capy {

/** Return the buffer unchanged.

    @param b The buffer to return.
    @return A copy of `b`, referring to the same storage.
*/
[[nodiscard]] inline
mutable_buffer
make_buffer(
    mutable_buffer const& b) noexcept
{
    return b;
}

/** Return the buffer, clamped to a maximum size.

    @param b The buffer to return.
    @param max_size The maximum size, in bytes, of the result.
    @return A buffer referring to the storage of `b` whose size
        is the smaller of `b.size()` and `max_size`.
*/
[[nodiscard]] inline
mutable_buffer
make_buffer(
    mutable_buffer const& b,
    std::size_t max_size) noexcept
{
    return mutable_buffer(
        b.data(),
        b.size() < max_size ? b.size() : max_size);
}

/** Return a buffer referring to a region of memory.

    @param data A pointer to the start of the region. The region
        must outlive the returned buffer.
    @param size The size of the region, in bytes.
    @return A buffer referring to `[data, data + size)`.
*/
[[nodiscard]] inline
mutable_buffer
make_buffer(
    void* data,
    std::size_t size) noexcept
{
    return mutable_buffer(data, size);
}

/** Return a buffer referring to a region of memory, clamped to a maximum size.

    @param data A pointer to the start of the region. The region
        must outlive the returned buffer.
    @param size The size of the region, in bytes.
    @param max_size The maximum size, in bytes, of the result.
    @return A buffer referring to `data` whose size is the smaller
        of `size` and `max_size`.
*/
[[nodiscard]] inline
mutable_buffer
make_buffer(
    void* data,
    std::size_t size,
    std::size_t max_size) noexcept
{
    return mutable_buffer(
        data,
        size < max_size ? size : max_size);
}

/** Return the buffer unchanged.

    @param b The buffer to return.
    @return A copy of `b`, referring to the same storage.
*/
[[nodiscard]] inline
const_buffer
make_buffer(
    const_buffer const& b) noexcept
{
    return b;
}

/** Return the buffer, clamped to a maximum size.

    @param b The buffer to return.
    @param max_size The maximum size, in bytes, of the result.
    @return A buffer referring to the storage of `b` whose size
        is the smaller of `b.size()` and `max_size`.
*/
[[nodiscard]] inline
const_buffer
make_buffer(
    const_buffer const& b,
    std::size_t max_size) noexcept
{
    return const_buffer(
        b.data(),
        b.size() < max_size ? b.size() : max_size);
}

/** Return a buffer referring to a region of memory.

    @param data A pointer to the start of the region. The region
        must outlive the returned buffer.
    @param size The size of the region, in bytes.
    @return A buffer referring to `[data, data + size)`.
*/
[[nodiscard]] inline
const_buffer
make_buffer(
    void const* data,
    std::size_t size) noexcept
{
    return const_buffer(data, size);
}

/** Return a buffer referring to a region of memory, clamped to a maximum size.

    @param data A pointer to the start of the region. The region
        must outlive the returned buffer.
    @param size The size of the region, in bytes.
    @param max_size The maximum size, in bytes, of the result.
    @return A buffer referring to `data` whose size is the smaller
        of `size` and `max_size`.
*/
[[nodiscard]] inline
const_buffer
make_buffer(
    void const* data,
    std::size_t size,
    std::size_t max_size) noexcept
{
    return const_buffer(
        data,
        size < max_size ? size : max_size);
}

// std::basic_string_view

/** Return a buffer from a `std::basic_string_view`.

    @param data The view whose characters are referenced. The
        underlying storage must outlive the returned buffer.
    @return A buffer referring to the view's storage. The size,
        in bytes, is `data.size() * sizeof(CharT)`.
*/
template<class CharT, class Traits>
[[nodiscard]]
const_buffer
make_buffer(
    std::basic_string_view<CharT, Traits> data) noexcept
{
    return const_buffer(
        data.size() ? data.data() : nullptr,
        data.size() * sizeof(CharT));
}

/** Return a buffer from a `std::basic_string_view`, clamped to a maximum size.

    @param data The view whose characters are referenced. The
        underlying storage must outlive the returned buffer.
    @param max_size The maximum size, in bytes, of the result.
    @return A buffer referring to the view's storage whose size is
        the smaller of `data.size() * sizeof(CharT)` and `max_size`.
*/
template<class CharT, class Traits>
[[nodiscard]]
const_buffer
make_buffer(
    std::basic_string_view<CharT, Traits> data,
    std::size_t max_size) noexcept
{
    return const_buffer(
        data.size() ? data.data() : nullptr,
        data.size() * sizeof(CharT) < max_size
            ? data.size() * sizeof(CharT) : max_size);
}

// Contiguous ranges

namespace detail {

template<class T>
concept non_buffer_contiguous_range =
    std::ranges::contiguous_range<T> &&
    std::ranges::sized_range<T> &&
    !std::convertible_to<T, const_buffer> &&
    !std::convertible_to<T, mutable_buffer> &&
    std::is_trivially_copyable_v<std::ranges::range_value_t<T>>;

template<class T>
concept mutable_contiguous_range =
    non_buffer_contiguous_range<T> &&
    !std::is_const_v<std::remove_reference_t<
        std::ranges::range_reference_t<T>>>;

template<class T>
concept const_contiguous_range =
    non_buffer_contiguous_range<T> &&
    std::is_const_v<std::remove_reference_t<
        std::ranges::range_reference_t<T>>>;

} // detail

/** Return a buffer from a mutable contiguous range.

    Accepts any sized, contiguous range of trivially-copyable,
    non-const elements, including `std::vector`, `std::array`,
    `std::string`, `std::span`, `boost::span`, and built-in arrays,
    whether passed as an lvalue or a temporary. The returned buffer
    refers to the range's storage, which must outlive the buffer.
    Its size, in bytes, is `size() * sizeof(element)`.
*/
template<detail::mutable_contiguous_range T>
[[nodiscard]]
mutable_buffer
make_buffer(T&& data) noexcept
{
    return mutable_buffer(
        std::ranges::size(data) ? std::ranges::data(data) : nullptr,
        std::ranges::size(data) * sizeof(std::ranges::range_value_t<T>));
}

/** Return a buffer from a mutable contiguous range, clamped to a maximum size.

    Like the unclamped overload, but the result is no larger than
    `max_size` bytes.

    @param data The range whose storage is referenced. It must
        outlive the returned buffer.
    @param max_size The maximum size, in bytes, of the result.
    @return A buffer whose size is the smaller of
        `size() * sizeof(element)` and `max_size`.
*/
template<detail::mutable_contiguous_range T>
[[nodiscard]]
mutable_buffer
make_buffer(
    T&& data,
    std::size_t max_size) noexcept
{
    auto const n = std::ranges::size(data) * sizeof(std::ranges::range_value_t<T>);
    return mutable_buffer(
        std::ranges::size(data) ? std::ranges::data(data) : nullptr,
        n < max_size ? n : max_size);
}

/** Return a buffer from a const contiguous range.

    Accepts any sized, contiguous range of trivially-copyable
    elements with const access, including const `std::vector`,
    `std::array`, `std::string`, `std::span`, `boost::span`, and
    string literals. The returned buffer refers to the range's
    storage, which must outlive the buffer. Its size, in bytes,
    is `size() * sizeof(element)`.
*/
template<detail::non_buffer_contiguous_range T>
[[nodiscard]]
const_buffer
make_buffer(T const& data) noexcept
{
    return const_buffer(
        std::ranges::size(data) ? std::ranges::data(data) : nullptr,
        std::ranges::size(data) * sizeof(std::ranges::range_value_t<T>));
}

/** Return a buffer from a const contiguous range, clamped to a maximum size.

    Like the unclamped overload, but the result is no larger than
    `max_size` bytes.

    @param data The range whose storage is referenced. It must
        outlive the returned buffer.
    @param max_size The maximum size, in bytes, of the result.
    @return A buffer whose size is the smaller of
        `size() * sizeof(element)` and `max_size`.
*/
template<detail::non_buffer_contiguous_range T>
[[nodiscard]]
const_buffer
make_buffer(
    T const& data,
    std::size_t max_size) noexcept
{
    auto const n = std::ranges::size(data) * sizeof(std::ranges::range_value_t<T>);
    return const_buffer(
        std::ranges::size(data) ? std::ranges::data(data) : nullptr,
        n < max_size ? n : max_size);
}

} // capy
} // boost

BOOST_CAPY_MSVC_WARNING_POP

#endif
