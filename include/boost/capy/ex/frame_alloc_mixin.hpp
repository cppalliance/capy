//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_FRAME_ALLOC_MIXIN_HPP
#define BOOST_CAPY_EX_FRAME_ALLOC_MIXIN_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/frame_memory_resource.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/recycling_memory_resource.hpp>

#include <cstddef>
#include <cstring>
#include <memory_resource>

namespace boost {
namespace capy {

/** Allocate a promise's coroutine frames from the current frame allocator.

    Inherit from this class in any coroutine promise type to opt into
    TLS-based frame allocation. The mixin provides `operator new` and
    `operator delete` that:

    1. Allocate from the current frame allocator (see
       @ref get_current_frame_allocator), or from the default memory
       resource if none is set.
    2. Deallocate through the same resource that allocated the frame,
       even if the current frame allocator changed in between.

    This is the same allocation strategy used by @ref
    context_awaitable_promise_base. Use this mixin directly when your
    promise type does not need the full environment and continuation
    support that `context_awaitable_promise_base` provides.

    @pre The resource that allocated a frame outlives that frame. Setting
    a different current frame allocator does not extend this lifetime.

    @par Thread Safety
    Allocation and deallocation are as thread-safe as the memory
    resource they use. The @ref recycling_memory_resource is safe to
    use from any thread.

    @par Example
    @par !example example


    @see @ref context_awaitable_promise_base, @ref recycling_memory_resource
*/
struct frame_alloc_mixin
{
    /** Allocate a coroutine frame.

        Allocates from the current frame allocator, or from the
        default memory resource if none is set. Deallocation later
        uses the same resource, even if the current frame allocator
        changed.

        @par Exception Safety
        Propagates any exception thrown by the underlying memory
        resource's `allocate`, for example `std::bad_alloc` from
        `::operator new`.

        @param size The size, in bytes, of the coroutine frame.

        @return A pointer to storage for the frame.
    */
    static void* operator new(std::size_t size)
    {
        // Calling the recycling resource directly skips virtual dispatch
        static auto* const rmr = get_recycling_memory_resource();

        auto* mr = get_current_frame_allocator();
        if(!mr)
            mr = std::pmr::get_default_resource();

        auto total = size + sizeof(std::pmr::memory_resource*);
        void* raw;
        if(mr == rmr)
            raw = static_cast<recycling_memory_resource*>(mr)
                ->allocate_fast(total, detail::frame_alignment);
        else
            raw = mr->allocate(total, detail::frame_alignment);
        // The trailing slot lets delete find the resource; memcpy avoids
        // an alignment requirement on it
        std::memcpy(static_cast<char*>(raw) + size, &mr, sizeof(mr));
        return raw;
    }

    /** Deallocate a coroutine frame.

        Returns the storage to the resource that allocated it,
        regardless of the current frame allocator.

        @param ptr The frame storage returned by `operator new`.

        @param size The size, in bytes, that was passed to `operator new`.
        This value must match.
    */
    static void operator delete(void* ptr, std::size_t size) noexcept
    {
        static auto* const rmr = get_recycling_memory_resource();

        std::pmr::memory_resource* mr;
        std::memcpy(&mr, static_cast<char*>(ptr) + size, sizeof(mr));
        auto total = size + sizeof(std::pmr::memory_resource*);
        if(mr == rmr)
            static_cast<recycling_memory_resource*>(mr)
                ->deallocate_fast(ptr, total, detail::frame_alignment);
        else
            mr->deallocate(ptr, total, detail::frame_alignment);
    }
};

namespace detail {

// For capy's own frames whose teardown can release the chain that owns
// the frame allocator: they are never allocated from it, so freeing them
// afterwards never reaches a resource that died with that chain.
struct process_frame_mixin
{
    static void* operator new(std::size_t size)
    {
        return static_cast<recycling_memory_resource*>(
            get_recycling_memory_resource())
                ->allocate_fast(size, detail::frame_alignment);
    }

    static void operator delete(void* ptr, std::size_t size) noexcept
    {
        static_cast<recycling_memory_resource*>(
            get_recycling_memory_resource())
                ->deallocate_fast(ptr, size, detail::frame_alignment);
    }
};

} // namespace detail

} // namespace capy
} // namespace boost

#endif
