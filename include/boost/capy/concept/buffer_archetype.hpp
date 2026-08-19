//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
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

/** Satisfies `ConstBufferSequence` without being default-constructible.

    This type satisfies @ref ConstBufferSequence but cannot be
    default-constructed. Use it only as an unevaluated parameter
    type in `requires`-clauses, to verify that a function template
    accepts any ConstBufferSequence.

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
    /// Default construction is not permitted.
    const_buffer_archetype_() = delete;

    /** Construct a copy.

        @param other The archetype to copy.
    */
    const_buffer_archetype_(const_buffer_archetype_ const& other) = default;

    /** Construct by moving.

        @param other The archetype to move from.
    */
    const_buffer_archetype_(const_buffer_archetype_&& other) = default;

    /** Assign by copying.

        @param other The archetype to copy.

        @return A reference to `*this`.
    */
    const_buffer_archetype_& operator=(const_buffer_archetype_ const& other) = default;

    /** Assign by moving.

        @param other The archetype to move from.

        @return A reference to `*this`.
    */
    const_buffer_archetype_& operator=(const_buffer_archetype_&& other) = default;

    /** Convert to const_buffer.

        @return An empty `const_buffer`.
    */
    operator const_buffer() const noexcept { return {}; }
};

#ifdef __clang__
/// Falls back to `const_buffer` itself: `const_buffer_archetype_` crashes clang.
using const_buffer_archetype = const_buffer;
#else
/// Picks `const_buffer_archetype_` to keep default construction rejected.
using const_buffer_archetype = const_buffer_archetype_;
#endif

/** Satisfies `MutableBufferSequence` without being default-constructible.

    This type satisfies @ref MutableBufferSequence but cannot be
    default-constructed. Use it only as an unevaluated parameter
    type in `requires`-clauses, to verify that a function template
    accepts any MutableBufferSequence.

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
    /// Default construction is not permitted.
    mutable_buffer_archetype_() = delete;

    /** Construct a copy.

        @param other The archetype to copy.
    */
    mutable_buffer_archetype_(mutable_buffer_archetype_ const& other) = default;

    /** Construct by moving.

        @param other The archetype to move from.
    */
    mutable_buffer_archetype_(mutable_buffer_archetype_&& other) = default;

    /** Assign by copying.

        @param other The archetype to copy.

        @return A reference to `*this`.
    */
    mutable_buffer_archetype_& operator=(mutable_buffer_archetype_ const& other) = default;

    /** Assign by moving.

        @param other The archetype to move from.

        @return A reference to `*this`.
    */
    mutable_buffer_archetype_& operator=(mutable_buffer_archetype_&& other) = default;

    /** Convert to mutable_buffer.

        @return An empty `mutable_buffer`.
    */
    operator mutable_buffer() const noexcept { return {}; }

    /** Convert to const_buffer.

        @return An empty `const_buffer`.
    */
    operator const_buffer() const noexcept { return {}; }
};

#ifdef __clang__
/// Falls back to `mutable_buffer` itself: `mutable_buffer_archetype_` crashes clang.
using mutable_buffer_archetype = mutable_buffer;
#else
/// Picks `mutable_buffer_archetype_` to keep default construction rejected.
using mutable_buffer_archetype = mutable_buffer_archetype_;
#endif

} // namespace capy
} // namespace boost

#endif
