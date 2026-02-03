//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
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
#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

/** Wrapper that adapts a standard Allocator to memory_resource.

    This wrapper is used to store value-type allocators in the
    execution context or trampoline frame. It rebinds the allocator
    to std::byte for raw memory allocation.

    @tparam Alloc The standard allocator type.
*/
template<class Alloc>
class frame_memory_resource : public std::pmr::memory_resource
{
    using traits = std::allocator_traits<Alloc>;
    using byte_alloc = typename traits::template rebind_alloc<std::byte>;
    using byte_traits = std::allocator_traits<byte_alloc>;

    static_assert(
        requires { typename traits::value_type; },
        "Alloc must satisfy allocator requirements");

    static_assert(
        std::is_copy_constructible_v<Alloc>,
        "Alloc must be copy constructible");

    byte_alloc alloc_;

public:
    /** Construct from an allocator.

        The allocator is rebind-copied to std::byte.

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
    do_allocate(std::size_t bytes, std::size_t) override
    {
        return byte_traits::allocate(alloc_, bytes);
    }

    void
    do_deallocate(void* p, std::size_t bytes, std::size_t) override
    {
        byte_traits::deallocate(alloc_, static_cast<std::byte*>(p), bytes);
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
