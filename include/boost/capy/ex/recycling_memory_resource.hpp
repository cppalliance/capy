//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_RECYCLING_MEMORY_RESOURCE_HPP
#define BOOST_CAPY_RECYCLING_MEMORY_RESOURCE_HPP

#include <boost/capy/detail/config.hpp>

#include <bit>
#include <cstddef>
#include <memory_resource>
#include <mutex>

namespace boost {
namespace capy {

/** Recycles freed blocks through per-thread pools, with a shared pool for cross-thread reuse.

    This memory resource recycles memory blocks using power-of-two
    size classes for O(1) allocation lookup. It maintains a thread-local
    pool for fast lock-free access and a global pool for cross-thread
    block sharing.

    Size classes: 64, 128, 256, 512, 1024, 2048 bytes.
    Allocations larger than 2048 bytes bypass the pools entirely.

    This is the default allocator used by run_async when no allocator
    is specified.

    @par Thread Safety
    Thread-safe. The thread-local pool requires no synchronization.
    The global pool uses a mutex for cross-thread access.

    @par Example
    @code
    auto* mr = get_recycling_memory_resource();
    run_async(ex, mr)(my_task());
    @endcode

    @see get_recycling_memory_resource
    @see run_async
*/
BOOST_CAPY_MSVC_WARNING_PUSH
BOOST_CAPY_MSVC_WARNING_DISABLE(4275) // non dll-interface base class
class BOOST_CAPY_DECL recycling_memory_resource : public std::pmr::memory_resource
{
    static constexpr std::size_t num_classes = 6;
    static constexpr std::size_t min_class_size = 64;   // 2^6
    static constexpr std::size_t max_class_size = 2048; // 2^11
    static constexpr std::size_t bucket_capacity = 16;

    static std::size_t
    round_up_pow2(std::size_t n) noexcept
    {
        return n <= min_class_size ? min_class_size : std::bit_ceil(n);
    }

    static std::size_t
    get_class_index(std::size_t rounded) noexcept
    {
        std::size_t idx = std::countr_zero(rounded) - 6;  // 64 = 2^6
        return idx < num_classes ? idx : num_classes;
    }

    struct bucket
    {
        std::size_t count = 0;
        void* ptrs[bucket_capacity] = {};

        void* pop() noexcept
        {
            if(count == 0)
                return nullptr;
            return ptrs[--count];
        }

        // Peter Dimov's idea
        void* pop(bucket& b) noexcept
        {
            if(count == 0)
                return nullptr;
            for(std::size_t i = 0; i < count; ++i)
                b.ptrs[i] = ptrs[i];
            b.count = count - 1;
            count = 0;
            return b.ptrs[b.count];
        }

        bool push(void* p) noexcept
        {
            if(count >= bucket_capacity)
                return false;
            ptrs[count++] = p;
            return true;
        }
    };

    struct pool
    {
        bucket buckets[num_classes];

        // No destructor: a non-trivial dtor forces a guard variable on the
        // thread_local in local(), checked on every alloc/free. Constant
        // initialization plus a trivial dtor makes that access a bare TLS
        // load. Cached blocks are instead reclaimed explicitly: per-thread
        // by arm_thread_cleanup() at thread exit, and the global pool by
        // global()'s holder destructor at process exit.
    };

    static pool& local() noexcept
    {
        static thread_local pool p;
        return p;
    }

    static pool& global() noexcept;
    static std::mutex& global_mutex() noexcept;

    void* allocate_slow(std::size_t rounded, std::size_t idx);
    void deallocate_slow(void* p, std::size_t idx);

    // Register a thread-exit callback that drains this thread's local
    // pool back to the OS. Called only off the hot path: unconditionally
    // from the slow paths, and once per thread from deallocate_fast
    // behind a guard-free flag.
    static void arm_thread_cleanup() noexcept;

public:
    /** Destroy the resource.

        No cached block is released here. Every pool is static, so an
        instance holds no state of its own. The thread-local pool is
        drained at thread exit, and the global pool at process exit.
    */
    ~recycling_memory_resource();

    /** Allocate without virtual dispatch.

        Handles the fast path inline (thread-local bucket pop)
        and falls through to the slow path for global pool or
        heap allocation.

        A request larger than the largest size class (2048 bytes)
        bypasses the pools and goes straight to `::operator new`.

        The second parameter is the requested alignment, and it is ignored.
        Every block comes from `::operator new`, so blocks carry the
        implementation's default new alignment and no more.

        @param bytes The number of bytes to allocate.

        @return A pointer to a block of at least `bytes` bytes. A pooled
        block is rounded up to its size class, so it may be larger than
        requested.

        @throws std::bad_alloc If the underlying `::operator new` fails.
    */
    void*
    allocate_fast(std::size_t bytes, std::size_t)
    {
        std::size_t rounded = round_up_pow2(bytes);
        std::size_t idx = get_class_index(rounded);
        if(idx >= num_classes)
            return ::operator new(bytes);
        auto& lp = local();
        if(auto* p = lp.buckets[idx].pop())
            return p;
        return allocate_slow(rounded, idx);
    }

    /** Deallocate without virtual dispatch.

        Handles the fast path inline (thread-local bucket push)
        and falls through to the slow path for global pool or
        heap deallocation.

        The block is cached in the pool of the thread that frees it, not
        the thread that allocated it.

        The third parameter is the alignment the block was allocated with,
        and it is ignored, as it is on allocation.

        @param p The block to return. It must have come from
        @ref allocate_fast or @ref do_allocate on this resource.

        @param bytes The size the block was allocated with. The size class
        is recomputed from it, so passing a different value puts the block
        in the wrong bucket.
    */
    void
    deallocate_fast(void* p, std::size_t bytes, std::size_t)
    {
        std::size_t rounded = round_up_pow2(bytes);
        std::size_t idx = get_class_index(rounded);
        if(idx >= num_classes)
        {
            ::operator delete(p);
            return;
        }
        // Guard-free flag (constinit bool, trivial dtor): arms thread-exit
        // cleanup exactly once for any thread that caches via deallocate,
        // including consumer threads that never hit a slow path.
        static thread_local bool armed = false;
        if(!armed)
        {
            armed = true;
            arm_thread_cleanup();
        }
        auto& lp = local();
        if(lp.buckets[idx].push(p))
            return;
        deallocate_slow(p, idx);
    }

protected:
    /** Allocate through the `std::pmr::memory_resource` interface.

        Forwards to @ref allocate_fast, so it has that function's contract.
        Call `allocate_fast` directly to skip the virtual dispatch.

        @param bytes The number of bytes to allocate.

        @param alignment The requested alignment. It is ignored.

        @return A pointer to a block of at least `bytes` bytes.

        @throws std::bad_alloc If the underlying `::operator new` fails.
    */
    void*
    do_allocate(std::size_t bytes, std::size_t alignment) override;

    /** Deallocate through the `std::pmr::memory_resource` interface.

        Forwards to @ref deallocate_fast, so it has that function's
        contract.

        @param p The block to return, as obtained from this resource.

        @param bytes The size the block was allocated with.

        @param alignment The alignment the block was allocated with. It is
        ignored.
    */
    void
    do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override;

    /** Compare this resource with another for equality.

        Equality is object identity: two distinct
        `recycling_memory_resource` objects compare unequal, even though the
        pools they draw from are static and therefore shared.

        @param other The resource to compare against.

        @return `true` if `other` is the same object as `*this`; otherwise
        `false`.
    */
    bool
    do_is_equal(const memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};
BOOST_CAPY_MSVC_WARNING_POP

/** Returns pointer to the default recycling memory resource.

    The returned pointer is valid for the lifetime of the program.
    This is the default allocator used by run_async.

    @return Pointer to the recycling memory resource.

    @see recycling_memory_resource
    @see run_async
*/
BOOST_CAPY_DECL
std::pmr::memory_resource*
get_recycling_memory_resource() noexcept;

} // namespace capy
} // namespace boost

#endif
