//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BUFFERS_BUFFER_PAIR_HPP
#define BOOST_CAPY_BUFFERS_BUFFER_PAIR_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <array>

namespace boost {
namespace capy {

/** A constant buffer pair
*/
using const_buffer_pair = std::array<const_buffer,2>;

/// Slice customization point for const_buffer_pair.
BOOST_CAPY_DECL
void
tag_invoke(
    slice_tag const&,
    const_buffer_pair& bs,
    slice_how how,
    std::size_t n) noexcept;

/** A mutable buffer pair
*/
using mutable_buffer_pair = std::array<mutable_buffer,2>;

/// Slice customization point for mutable_buffer_pair.
BOOST_CAPY_DECL
void
tag_invoke(
    slice_tag const&,
    mutable_buffer_pair& bs,
    slice_how how,
    std::size_t n) noexcept;

} // capy
} // boost

#endif
