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

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4459)
#endif

namespace boost {
namespace capy {

/** Return a buffer.
*/
[[nodiscard]] inline
mutable_buffer
make_buffer(
    mutable_buffer const& b) noexcept
{
    return b;
}

/** Return a buffer with a maximum size.
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

/** Return a buffer.
*/
[[nodiscard]] inline
mutable_buffer
make_buffer(
    void* data,
    std::size_t size) noexcept
{
    return mutable_buffer(data, size);
}

/** Return a buffer with a maximum size.
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

/** Return a buffer.
*/
[[nodiscard]] inline
const_buffer
make_buffer(
    const_buffer const& b) noexcept
{
    return b;
}

/** Return a buffer with a maximum size.
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

/** Return a buffer.
*/
[[nodiscard]] inline
const_buffer
make_buffer(
    void const* data,
    std::size_t size) noexcept
{
    return const_buffer(data, size);
}

/** Return a buffer with a maximum size.
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

/** Return a buffer from a C-style array.
*/
template<class T, std::size_t N>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
mutable_buffer
make_buffer(
    T (&data)[N]) noexcept
{
    return mutable_buffer(
        data, N * sizeof(T));
}

/** Return a buffer from a C-style array with a maximum size.
*/
template<class T, std::size_t N>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
mutable_buffer
make_buffer(
    T (&data)[N],
    std::size_t max_size) noexcept
{
    return mutable_buffer(
        data,
        N * sizeof(T) < max_size ? N * sizeof(T) : max_size);
}

/** Return a buffer from a const C-style array.
*/
template<class T, std::size_t N>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
const_buffer
make_buffer(
    T const (&data)[N]) noexcept
{
    return const_buffer(
        data, N * sizeof(T));
}

/** Return a buffer from a const C-style array with a maximum size.
*/
template<class T, std::size_t N>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
const_buffer
make_buffer(
    T const (&data)[N],
    std::size_t max_size) noexcept
{
    return const_buffer(
        data,
        N * sizeof(T) < max_size ? N * sizeof(T) : max_size);
}

// std::array

/** Return a buffer from a std::array.
*/
template<class T, std::size_t N>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
mutable_buffer
make_buffer(
    std::array<T, N>& data) noexcept
{
    return mutable_buffer(
        data.data(), data.size() * sizeof(T));
}

/** Return a buffer from a std::array with a maximum size.
*/
template<class T, std::size_t N>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
mutable_buffer
make_buffer(
    std::array<T, N>& data,
    std::size_t max_size) noexcept
{
    return mutable_buffer(
        data.data(),
        data.size() * sizeof(T) < max_size
            ? data.size() * sizeof(T) : max_size);
}

/** Return a buffer from a const std::array.
*/
template<class T, std::size_t N>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
const_buffer
make_buffer(
    std::array<T, N> const& data) noexcept
{
    return const_buffer(
        data.data(), data.size() * sizeof(T));
}

/** Return a buffer from a const std::array with a maximum size.
*/
template<class T, std::size_t N>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
const_buffer
make_buffer(
    std::array<T, N> const& data,
    std::size_t max_size) noexcept
{
    return const_buffer(
        data.data(),
        data.size() * sizeof(T) < max_size
            ? data.size() * sizeof(T) : max_size);
}

// std::vector

/** Return a buffer from a std::vector.
*/
template<class T, class Allocator>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
mutable_buffer
make_buffer(
    std::vector<T, Allocator>& data) noexcept
{
    return mutable_buffer(
        data.size() ? data.data() : nullptr,
        data.size() * sizeof(T));
}

/** Return a buffer from a std::vector with a maximum size.
*/
template<class T, class Allocator>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
mutable_buffer
make_buffer(
    std::vector<T, Allocator>& data,
    std::size_t max_size) noexcept
{
    return mutable_buffer(
        data.size() ? data.data() : nullptr,
        data.size() * sizeof(T) < max_size
            ? data.size() * sizeof(T) : max_size);
}

/** Return a buffer from a const std::vector.
*/
template<class T, class Allocator>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
const_buffer
make_buffer(
    std::vector<T, Allocator> const& data) noexcept
{
    return const_buffer(
        data.size() ? data.data() : nullptr,
        data.size() * sizeof(T));
}

/** Return a buffer from a const std::vector with a maximum size.
*/
template<class T, class Allocator>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]]
const_buffer
make_buffer(
    std::vector<T, Allocator> const& data,
    std::size_t max_size) noexcept
{
    return const_buffer(
        data.size() ? data.data() : nullptr,
        data.size() * sizeof(T) < max_size
            ? data.size() * sizeof(T) : max_size);
}

// std::basic_string

/** Return a buffer from a std::basic_string.
*/
template<class CharT, class Traits, class Allocator>
[[nodiscard]]
mutable_buffer
make_buffer(
    std::basic_string<CharT, Traits, Allocator>& data) noexcept
{
    return mutable_buffer(
        data.size() ? &data[0] : nullptr,
        data.size() * sizeof(CharT));
}

/** Return a buffer from a std::basic_string with a maximum size.
*/
template<class CharT, class Traits, class Allocator>
[[nodiscard]]
mutable_buffer
make_buffer(
    std::basic_string<CharT, Traits, Allocator>& data,
    std::size_t max_size) noexcept
{
    return mutable_buffer(
        data.size() ? &data[0] : nullptr,
        data.size() * sizeof(CharT) < max_size
            ? data.size() * sizeof(CharT) : max_size);
}

/** Return a buffer from a const std::basic_string.
*/
template<class CharT, class Traits, class Allocator>
[[nodiscard]]
const_buffer
make_buffer(
    std::basic_string<CharT, Traits, Allocator> const& data) noexcept
{
    return const_buffer(
        data.data(),
        data.size() * sizeof(CharT));
}

/** Return a buffer from a const std::basic_string with a maximum size.
*/
template<class CharT, class Traits, class Allocator>
[[nodiscard]]
const_buffer
make_buffer(
    std::basic_string<CharT, Traits, Allocator> const& data,
    std::size_t max_size) noexcept
{
    return const_buffer(
        data.data(),
        data.size() * sizeof(CharT) < max_size
            ? data.size() * sizeof(CharT) : max_size);
}

// std::basic_string_view

/** Return a buffer from a std::basic_string_view.
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

/** Return a buffer from a std::basic_string_view with a maximum size.
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

// std::span

/** Return a buffer from a mutable std::span.
*/
template<class T, std::size_t Extent>
    requires (!std::is_const_v<T> && sizeof(T) == 1)
[[nodiscard]]
mutable_buffer
make_buffer(
    std::span<T, Extent> data) noexcept
{
    return mutable_buffer(data.data(), data.size());
}

/** Return a buffer from a mutable std::span with a maximum size.
*/
template<class T, std::size_t Extent>
    requires (!std::is_const_v<T> && sizeof(T) == 1)
[[nodiscard]]
mutable_buffer
make_buffer(
    std::span<T, Extent> data,
    std::size_t max_size) noexcept
{
    return mutable_buffer(
        data.data(),
        data.size() < max_size ? data.size() : max_size);
}

/** Return a buffer from a const std::span.
*/
template<class T, std::size_t Extent>
    requires (sizeof(T) == 1)
[[nodiscard]]
const_buffer
make_buffer(
    std::span<T const, Extent> data) noexcept
{
    return const_buffer(data.data(), data.size());
}

/** Return a buffer from a const std::span with a maximum size.
*/
template<class T, std::size_t Extent>
    requires (sizeof(T) == 1)
[[nodiscard]]
const_buffer
make_buffer(
    std::span<T const, Extent> data,
    std::size_t max_size) noexcept
{
    return const_buffer(
        data.data(),
        data.size() < max_size ? data.size() : max_size);
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
*/
template<detail::mutable_contiguous_range T>
[[nodiscard]]
mutable_buffer
make_buffer(T& data) noexcept
{
    return mutable_buffer(
        std::ranges::size(data) ? std::ranges::data(data) : nullptr,
        std::ranges::size(data) * sizeof(std::ranges::range_value_t<T>));
}

/** Return a buffer from a mutable contiguous range with a maximum size.
*/
template<detail::mutable_contiguous_range T>
[[nodiscard]]
mutable_buffer
make_buffer(
    T& data,
    std::size_t max_size) noexcept
{
    auto const n = std::ranges::size(data) * sizeof(std::ranges::range_value_t<T>);
    return mutable_buffer(
        std::ranges::size(data) ? std::ranges::data(data) : nullptr,
        n < max_size ? n : max_size);
}

/** Return a buffer from a const contiguous range.
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

/** Return a buffer from a const contiguous range with a maximum size.
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

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
