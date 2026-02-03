//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_BUFFER_ARCHETYPE_HPP
#define BOOST_CAPY_CONCEPT_BUFFER_ARCHETYPE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>

namespace boost {
namespace capy {

/** Archetype for ConstBufferSequence concept checking.

    This type satisfies @ref ConstBufferSequence but cannot be
    instantiated. Use it in concept definitions to verify that
    a function template accepts any ConstBufferSequence.

    @par Example
    @code
    template<typename T>
    concept MyWritable =
        requires(T& stream, const_buffer_archetype buffers)
        {
            stream.write(buffers);
        };
    @endcode
*/
struct const_buffer_archetype_
{
    const_buffer_archetype_() = delete;
    const_buffer_archetype_(const_buffer_archetype_ const&) = default;
    const_buffer_archetype_(const_buffer_archetype_&&) = default;
    const_buffer_archetype_& operator=(const_buffer_archetype_ const&) = default;
    const_buffer_archetype_& operator=(const_buffer_archetype_&&) = default;

    operator const_buffer() const noexcept { return {}; }
};

#ifdef __clang__
// workaround: archetype crashes clang
using const_buffer_archetype = const_buffer;
#else
using const_buffer_archetype = const_buffer_archetype_;
#endif

//------------------------------------------------

/** Archetype for MutableBufferSequence concept checking.

    This type satisfies @ref MutableBufferSequence but cannot be
    instantiated. Use it in concept definitions to verify that
    a function template accepts any MutableBufferSequence.

    @par Example
    @code
    template<typename T>
    concept MyReadable =
        requires(T& stream, mutable_buffer_archetype buffers)
        {
            stream.read(buffers);
        };
    @endcode
*/
struct mutable_buffer_archetype_
{
    mutable_buffer_archetype_() = delete;
    mutable_buffer_archetype_(mutable_buffer_archetype_ const&) = default;
    mutable_buffer_archetype_(mutable_buffer_archetype_&&) = default;
    mutable_buffer_archetype_& operator=(mutable_buffer_archetype_ const&) = default;
    mutable_buffer_archetype_& operator=(mutable_buffer_archetype_&&) = default;

    operator mutable_buffer() const noexcept { return {}; }
    operator const_buffer() const noexcept { return {}; }
};

#ifdef __clang__
// workaround: archetype crashes clang
using mutable_buffer_archetype = mutable_buffer;
#else
using mutable_buffer_archetype = mutable_buffer_archetype_;
#endif

} // namespace capy
} // namespace boost

#endif
