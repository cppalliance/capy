//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_READ_SOURCE_HPP
#define BOOST_CAPY_BUFFERS_READ_SOURCE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers/detail/except.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/system/error_code.hpp>

namespace boost {
namespace capy {
namespace buffers {

/** Metafunction to detect if a type is a read source.

    Data is obtained from a read source by calling @ref read one
    or more times with caller-provided buffers, until the function
    returns @ref error::eof.

    The members `size` and `rewind` are optional. The metafunctions
    @ref has_size and @ref has_rewind may be used to detect the
    availability of these capabilities.

    @code
    struct ReadSource
    {
        // (optional)
        std::uint64_t size() const noexcept;

        // (optional)
        void rewind();

        // returns error::eof when no more data
        template< class MutableBufferSequence >
        std::size_t
        read(
            MutableBufferSequence const& buffers,
            system::error_code& ec);
    };
    @endcode
*/
template<class T, class = void>
struct is_read_source
    : std::false_type
{
};

template<class T>
struct is_read_source<T, std::void_t<typename
    std::enable_if<
        std::is_convertible<
            decltype(std::declval<T&>().read(
                std::declval<mutable_buffer>(),
                std::declval<system::error_code&>())),
            std::size_t>::value>::type>>
    : std::true_type
{
};

/** Metafunction to determine if `T::has_size` exists.
*/
template<class T, class = void>
struct has_size : std::false_type
{
};

template<class T>
struct has_size<T, std::void_t<
    typename std::enable_if<
        std::is_convertible<
            decltype(std::declval<T const&>().size()),
            std::uint64_t>::value>::type>>
    : std::true_type
{
};

/** Metafunction to determin if `T::has_rewind` exists.
*/
template<class T, class = void>
struct has_rewind : std::false_type
{
};

template<class T>
struct has_rewind<T, std::void_t<decltype(
    std::declval<T&>().rewind())>>
    : std::true_type
{
};

} // buffers
} // capy
} // boost

#endif
