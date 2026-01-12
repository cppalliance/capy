//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_DYNAMIC_BUFFER_HPP
#define BOOST_CAPY_CONCEPT_DYNAMIC_BUFFER_HPP

#include <boost/capy/buffers.hpp>

#include <cstddef>

namespace boost {
namespace capy {
namespace buffers {

/** Concept for types that model DynamicBuffer.
*/
template<class T>
concept dynamic_buffer =
    requires(T& t, T const& ct, std::size_t n)
    {
        typename T::const_buffers_type;
        typename T::mutable_buffers_type;
        { ct.size() } -> std::convertible_to<std::size_t>;
        { ct.max_size() } -> std::convertible_to<std::size_t>;
        { ct.capacity() } -> std::convertible_to<std::size_t>;
        { ct.data() } -> std::same_as<typename T::const_buffers_type>;
        { t.prepare(n) } -> std::same_as<typename T::mutable_buffers_type>;
        t.commit(n);
        t.consume(n);
    } &&
    const_buffer_sequence<typename T::const_buffers_type> &&
    mutable_buffer_sequence<typename T::mutable_buffers_type>;

} // buffers
} // capy
} // boost

#endif
