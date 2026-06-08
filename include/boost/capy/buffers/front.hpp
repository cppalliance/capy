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

namespace detail {
struct front_fn
{
    /// Return the first mutable buffer, or an empty buffer.
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

    /// Return the first const buffer, or an empty buffer.
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
};
} // detail

/** Return the first buffer in a sequence.

    For a `MutableBufferSequence` the result is a `mutable_buffer`;
    otherwise it is a `const_buffer`.

    @param bs The buffer sequence.

    @return The first buffer in the sequence, or an empty buffer if the
        sequence is empty.
*/
constexpr detail::front_fn const front{};

} // capy
} // boost

#endif
