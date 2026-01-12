//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_COPY_HPP
#define BOOST_CAPY_BUFFERS_COPY_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <cstring>
#include <utility>

namespace boost {
namespace capy {

/** Copy the contents of a buffer sequence into another buffer sequence

    This function copies no more than `at_most` bytes from the constant buffer
    sequence denoted by `src` into the mutable buffer sequence denoted by `dest`.

    @par Constraints
    @code
    mutable_buffer_sequence<decltype(dest)> &&
    const_buffer_sequence<decltype(src)>
    @endcode

    @return The number of bytes actually copied, which will be exactly equal to
    `std::min( size(dest), size(src), at_most )`.

    @param dest The destination buffer sequence

    @param src The source buffer sequence
*/
constexpr struct copy_mrdocs_workaround_t
{
    template<
        mutable_buffer_sequence MutableBufferSequence,
        const_buffer_sequence ConstBufferSequence>
    std::size_t
    operator()(
        MutableBufferSequence const& dest,
        ConstBufferSequence const& src,
        std::size_t at_most = std::size_t(-1)) const noexcept
    {
        std::size_t total = 0;
        std::size_t pos0 = 0;
        std::size_t pos1 = 0;
        auto const end0 = end(src);
        auto const end1 = end(dest);
        auto it0 = begin(src);
        auto it1 = begin(dest);
        while(
            total < at_most &&
            it0 != end0 &&
            it1 != end1)
        {
            const_buffer b0 = *it0;
            mutable_buffer b1 = *it1;
            b0 += pos0;
            b1 += pos1;
            std::size_t const amount =
            [&]
            {
                std::size_t n = b0.size();
                if( n > b1.size())
                    n = b1.size();
                if( n > at_most - total)
                    n = at_most - total;
                if(n != 0)
                    std::memcpy(
                        b1.data(),
                        b0.data(),
                        n);
                return n;
            }();
            total += amount;
            if(amount == b1.size())
            {
                ++it1;
                pos1 = 0;
            }
            else
            {
                pos1 += amount;
            }
            if(amount == b0.size())
            {
                ++it0;
                pos0 = 0;
            }
            else
            {
                pos0 += amount;
            }
        }
        return total;
    }
} copy {};

} // capy
} // boost

#endif
