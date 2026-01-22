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

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/const_buffer_sequence.hpp>
#include <boost/capy/concept/mutable_buffer_sequence.hpp>

#include <cstddef>

namespace boost {
namespace capy {

/** Concept for types that model DynamicBuffer.
*/
template<class T>
concept DynamicBuffer =
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
    ConstBufferSequence<typename T::const_buffers_type> &&
    MutableBufferSequence<typename T::mutable_buffers_type>;

} // capy
} // boost

#endif
