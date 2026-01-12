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
#include <boost/capy/buffers/buffer.hpp>
#include <boost/capy/buffers/range.hpp>

namespace boost {
namespace capy {
namespace buffers {

/** Return the first buffer in a sequence.
*/
constexpr struct front_mrdocs_workaround_t
{
    template<mutable_buffer_sequence MutableBufferSequence>
    mutable_buffer
    operator()(
        MutableBufferSequence const& bs) const noexcept
    {
        auto const it = begin(bs);
        if(it != end(bs))
            return *it;
        return {};
    }

    template<const_buffer_sequence ConstBufferSequence>
        requires (!mutable_buffer_sequence<ConstBufferSequence>)
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

} // buffers
} // capy
} // boost

#endif
