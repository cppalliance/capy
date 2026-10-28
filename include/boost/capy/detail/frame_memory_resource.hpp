//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_FRAME_MEMORY_RESOURCE_HPP
#define BOOST_CAPY_DETAIL_FRAME_MEMORY_RESOURCE_HPP

#include <boost/capy/detail/config.hpp>

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

/** The alignment capy requests for every coroutine frame.

    The compiler assumes a frame's `operator new` returns memory
    aligned to `__STDCPP_DEFAULT_NEW_ALIGNMENT__`. On some targets,
    such as MSVC and Apple arm64, that is 16 while
    `alignof(std::max_align_t)` is only 8.
*/
inline constexpr std::size_t frame_alignment =
    static_cast<std::size_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__) >
        alignof(std::max_align_t)
    ? static_cast<std::size_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__)
    : alignof(std::max_align_t);

/** One unit of frame storage.

    A standard allocator takes no alignment argument, so frame memory
    is allocated through it as an array of units: every block is then
    aligned for any frame. A unit is exactly as wide as its alignment,
    which can be less than `sizeof(std::max_align_t)`, so a block
    wastes less than one alignment of padding.
*/
struct alignas(frame_alignment) frame_unit
{
    unsigned char bytes[frame_alignment];
};

/** Return the number of frame units that hold `bytes`.

    Rounds up without overflowing for any `bytes`.
*/
inline constexpr std::size_t
frame_units(std::size_t bytes) noexcept
{
    return bytes / sizeof(frame_unit) +
        (bytes % sizeof(frame_unit) != 0);
}

/** Wrapper that adapts a standard Allocator to memory_resource.

    This wrapper is used to store value-type allocators in the
    execution context or trampoline frame. It rebinds the allocator
    to `frame_unit` and allocates whole units of it, so every
    block is aligned for any frame. A standard allocator takes no
    alignment argument, and one rebound to `std::byte` may pack blocks
    at byte alignment.

    A request for alignment above `frame_alignment` throws
    `std::bad_alloc`.

    @tparam Alloc The standard allocator type.
*/
template<class Alloc>
class frame_memory_resource : public std::pmr::memory_resource
{
    using traits = std::allocator_traits<Alloc>;
    using unit_alloc =
        typename traits::template rebind_alloc<frame_unit>;
    using unit_traits = std::allocator_traits<unit_alloc>;

    static_assert(
        requires { typename traits::value_type; },
        "Alloc must satisfy allocator requirements");

    static_assert(
        std::is_copy_constructible_v<Alloc>,
        "Alloc must be copy constructible");

    unit_alloc alloc_;

public:
    /** Construct from an allocator.

        The allocator is rebind-copied to `frame_unit`.

        @param a The allocator to adapt.
    */
    explicit
    frame_memory_resource(Alloc const& a)
        : alloc_(a)
    {
    }

    /** Get the memory resource pointer.

        @return Pointer to this memory resource.
    */
    std::pmr::memory_resource* get() noexcept { return this; }

protected:
    void*
    do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        // A block is only as aligned as a unit. The memory_resource
        // contract allows a throw, never a misaligned block.
        if(alignment > alignof(frame_unit))
            throw std::bad_alloc();
        return unit_traits::allocate(alloc_, frame_units(bytes));
    }

    void
    do_deallocate(void* p, std::size_t bytes, std::size_t) override
    {
        unit_traits::deallocate(
            alloc_, static_cast<frame_unit*>(p), frame_units(bytes));
    }

    bool
    do_is_equal(const memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

} // namespace detail
} // namespace capy
} // namespace boost

#endif
