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
#include <boost/capy/buffers/buffer.hpp>
#include <cstdlib>
#include <type_traits>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4459)
#endif

namespace boost {
namespace capy {
namespace buffers {

/** Return a buffer.
*/
inline
mutable_buffer
make_buffer(
    mutable_buffer const& b) noexcept
{
    return b;
}

/** Return a buffer.
*/
inline
mutable_buffer
make_buffer(
    void* data,
    std::size_t size) noexcept
{
    return mutable_buffer(data, size);
}

/** Return a buffer.
*/
inline
const_buffer
make_buffer(
    const_buffer const& b) noexcept
{
    return b;
}

/** Return a buffer.
*/
inline
const_buffer
make_buffer(
    void const* data,
    std::size_t size) noexcept
{
    return const_buffer(data, size);
}

/** Return a buffer.
*/
template<
    class T, std::size_t N
    , class = typename std::enable_if<
        std::is_trivially_copyable<T>::value>::type
>
mutable_buffer
make_buffer(
    T (&data)[N]) noexcept
{
    return mutable_buffer(
        data, N * sizeof(T));
}

/** Return a buffer.
*/
template<
    class T, std::size_t N
    , class = typename std::enable_if<
        std::is_trivially_copyable<T>::value>::type
>
const_buffer
make_buffer(
    T const (&data)[N]) noexcept
{
    return const_buffer(
        data, N * sizeof(T));
}

} // buffers
} // capy
} // boost

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
