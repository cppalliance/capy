//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_FRONT_HPP
#define BOOST_CAPY_BUFFERS_FRONT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>

namespace boost {
namespace capy {

/** Return the first buffer in a sequence.

    @functionobject
*/
constexpr struct
{
    /** Return the first mutable buffer, or an empty buffer.

        @param bs The buffer sequence to inspect.

        @return The first buffer in `bs`, or an empty buffer if `bs`
        contains no buffers.
    */
    template<MutableBufferSequence MutableBufferSequence>
    mutable_buffer
    operator()(
        MutableBufferSequence const& bs) const noexcept
    {
        auto const it = begin(bs);
        if(it != end(bs))
            return *it;
        return {};
    }

    /** Return the first const buffer, or an empty buffer.

        @param bs The buffer sequence to inspect.

        @return The first buffer in `bs`, or an empty buffer if `bs`
        contains no buffers.
    */
    template<ConstBufferSequence ConstBufferSequence>
        requires (!MutableBufferSequence<ConstBufferSequence>)
    const_buffer
    operator()(
        ConstBufferSequence const& bs) const noexcept
    {
        auto const it = begin(bs);
        if(it != end(bs))
            return *it;
        return {};
    }
} const front{};

} // capy
} // boost

#endif
