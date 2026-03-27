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
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/recycling_memory_resource.hpp>

#include <cstddef>
#include <cstring>
#include <memory_resource>

namespace boost {
namespace capy {

/** Mixin that adds frame-allocator-aware allocation to a promise type.

    Inherit from this class in any coroutine promise type to opt into
    TLS-based frame allocation with the recycling memory resource
    fast path. The mixin provides `operator new` and `operator delete`
    that:

    1. Read the thread-local frame allocator set by `run_async` or `run`.
    2. Bypass virtual dispatch when the allocator is the default
       recycling memory resource.
    3. Store the allocator pointer at the end of each frame for
       correct deallocation even when TLS changes between allocation
       and deallocation.

    This is the same allocation strategy used by @ref
    io_awaitable_promise_base. Use this mixin directly when your
    promise type does not need the full environment and continuation
    support that `io_awaitable_promise_base` provides.

    @par Example
    @code
    struct my_internal_coroutine
    {
        struct promise_type : frame_alloc_mixin
        {
            my_internal_coroutine get_return_object();
            std::suspend_always initial_suspend() noexcept;
            std::suspend_always final_suspend() noexcept;
            void return_void();
            void unhandled_exception() noexcept;
        };
    };
    @endcode

    @par Thread Safety
    The allocation fast path uses thread-local storage and requires
    no synchronization. The global pool fallback is mutex-protected.

    @see io_awaitable_promise_base, frame_allocator, recycling_memory_resource
*/
struct frame_alloc_mixin
{
    /** Allocate a coroutine frame.

        Uses the thread-local frame allocator set by run_async.
        Falls back to default memory resource if not set.
        Stores the allocator pointer at the end of each frame for
        correct deallocation even when TLS changes. Uses memcpy
        to avoid alignment requirements on the trailing pointer.
        Bypasses virtual dispatch for the recycling allocator.
    */
    static void* operator new(std::size_t size)
    {
        static auto* const rmr = get_recycling_memory_resource();

        auto* mr = get_current_frame_allocator();
        if(!mr)
            mr = std::pmr::get_default_resource();

        auto total = size + sizeof(std::pmr::memory_resource*);
        void* raw;
        if(mr == rmr)
            raw = static_cast<recycling_memory_resource*>(mr)
                ->allocate_fast(total, alignof(std::max_align_t));
        else
            raw = mr->allocate(total, alignof(std::max_align_t));
        std::memcpy(static_cast<char*>(raw) + size, &mr, sizeof(mr));
        return raw;
    }

    /** Deallocate a coroutine frame.

        Reads the allocator pointer stored at the end of the frame
        to ensure correct deallocation regardless of current TLS.
        Bypasses virtual dispatch for the recycling allocator.
    */
    static void operator delete(void* ptr, std::size_t size) noexcept
    {
        static auto* const rmr = get_recycling_memory_resource();

        std::pmr::memory_resource* mr;
        std::memcpy(&mr, static_cast<char*>(ptr) + size, sizeof(mr));
        auto total = size + sizeof(std::pmr::memory_resource*);
        if(mr == rmr)
            static_cast<recycling_memory_resource*>(mr)
                ->deallocate_fast(ptr, total, alignof(std::max_align_t));
        else
            mr->deallocate(ptr, total, alignof(std::max_align_t));
    }
};

} // namespace capy
} // namespace boost

#endif
