//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_FRAME_ALLOCATOR_HPP
#define BOOST_CAPY_FRAME_ALLOCATOR_HPP

#include <boost/capy/detail/config.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>

namespace boost {
namespace capy {

//----------------------------------------------------------
// TLS Accessor
//----------------------------------------------------------

/** Thread-local storage for the current frame allocator.

    This function returns a reference to the thread-local pointer
    that holds the current memory_resource for frame allocation.
    The pointer is set by run_async before creating any tasks.

    @return Reference to the thread-local memory_resource pointer.
*/
inline std::pmr::memory_resource*&
current_frame_allocator() noexcept
{
    static thread_local std::pmr::memory_resource* mr = nullptr;
    return mr;
}

//----------------------------------------------------------
// frame_memory_resource
//----------------------------------------------------------

/** Wrapper that adapts a standard Allocator to memory_resource.

    This wrapper is used to store value-type allocators in the
    trampoline frame. It rebinds the allocator to std::byte for
    raw memory allocation.

    memory_resource* is stored directly in the trampoline without
    wrapping to avoid double indirection.

    @tparam Alloc The standard allocator type.
*/
template<class Alloc>
class frame_memory_resource : public std::pmr::memory_resource
{
    using traits = std::allocator_traits<Alloc>;
    using byte_alloc = typename traits::template rebind_alloc<std::byte>;
    using byte_traits = std::allocator_traits<byte_alloc>;

    static_assert(std::is_copy_constructible_v<Alloc>,
        "Allocator must be copy constructible");

    byte_alloc alloc_;

public:
    /** Construct from an allocator.

        The allocator is rebind-copied to std::byte.

        @param a The allocator to adapt.
    */
    frame_memory_resource(Alloc a)
        : alloc_(a)
    {
    }

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

} // namespace capy
} // namespace boost

#endif
