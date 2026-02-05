//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/buffers/buffer_array.hpp>

namespace boost {
namespace capy {
namespace detail {

namespace {

template<class Buffer>
void
do_remove_prefix(
    Buffer* arr,
    std::size_t* count,
    std::size_t* total_size,
    std::size_t n) noexcept
{
    if(n >= *total_size)
    {
        while(*count > 0)
            arr[--(*count)].~Buffer();
        *total_size = 0;
        return;
    }

    std::size_t i = 0;
    while(i < *count && n > 0)
    {
        auto& b = arr[i];
        if(n < b.size())
        {
            b += n;
            *total_size -= n;
            break;
        }
        n -= b.size();
        *total_size -= b.size();
        b.~Buffer();
        ++i;
    }

    // Compact: move remaining buffers to front
    if(i > 0)
    {
        std::size_t j = 0;
        while(i < *count)
        {
            arr[j] = arr[i];
            arr[i].~Buffer();
            ++i;
            ++j;
        }
        *count = j;
    }
}

template<class Buffer>
void
do_keep_prefix(
    Buffer* arr,
    std::size_t* count,
    std::size_t* total_size,
    std::size_t n) noexcept
{
    if(n >= *total_size)
        return;

    if(n == 0)
    {
        while(*count > 0)
            arr[--(*count)].~Buffer();
        *total_size = 0;
        return;
    }

    std::size_t remaining = n;
    std::size_t i = 0;
    while(i < *count && remaining > 0)
    {
        auto& b = arr[i];
        if(remaining < b.size())
        {
            b = Buffer(b.data(), remaining);
            ++i;
            break;
        }
        remaining -= b.size();
        ++i;
    }

    // Destruct elements beyond the new count
    while(*count > i)
        arr[--(*count)].~Buffer();

    *total_size = n;
}

} // anonymous namespace

void
buffer_array_remove_prefix(
    const_buffer* arr,
    std::size_t* count,
    std::size_t* total_size,
    std::size_t n) noexcept
{
    do_remove_prefix(arr, count, total_size, n);
}

void
buffer_array_remove_prefix(
    mutable_buffer* arr,
    std::size_t* count,
    std::size_t* total_size,
    std::size_t n) noexcept
{
    do_remove_prefix(arr, count, total_size, n);
}

void
buffer_array_keep_prefix(
    const_buffer* arr,
    std::size_t* count,
    std::size_t* total_size,
    std::size_t n) noexcept
{
    do_keep_prefix(arr, count, total_size, n);
}

void
buffer_array_keep_prefix(
    mutable_buffer* arr,
    std::size_t* count,
    std::size_t* total_size,
    std::size_t n) noexcept
{
    do_keep_prefix(arr, count, total_size, n);
}

} // namespace detail
} // namespace capy
} // namespace boost
