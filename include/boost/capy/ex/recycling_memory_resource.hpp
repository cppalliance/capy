//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
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

/** Recycling memory resource with size-class buckets.

    This memory resource recycles memory blocks using power-of-two
    size classes for O(1) allocation lookup. It maintains a thread-local
    pool for fast lock-free access and a global pool for cross-thread
    block sharing.

    Size classes: 64, 128, 256, 512, 1024, 2048 bytes.
    Allocations larger than 2048 bytes bypass the pools entirely.

    This is the default allocator used by run_async when no allocator
    is specified.

    @note This resource honors only the default new alignment
    (`__STDCPP_DEFAULT_NEW_ALIGNMENT__`, typically
    `alignof(std::max_align_t)`). The alignment argument passed to
    `do_allocate`/`do_deallocate` (and to `allocate_fast`/`deallocate_fast`)
    is ignored; backing storage comes from `::operator new`. Over-aligned
    requests are therefore not satisfied. This is sufficient for coroutine
    frame allocation but means the resource cannot be used where
    over-aligned memory is required.

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
        void* ptrs[bucket_capacity];

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

        ~pool()
        {
            for(auto& b : buckets)
                while(b.count > 0)
                    ::operator delete(b.pop());
        }
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

public:
    /** Destructor.

        Releases any blocks still held in this resource's thread-local
        pool for the calling thread. Blocks held in the process-wide
        global pool, and in other threads' thread-local pools, are
        released when those pools are destroyed.
    */
    ~recycling_memory_resource();

    /** Allocate without virtual dispatch.

        Handles the fast path inline (thread-local bucket pop)
        and falls through to the slow path for global pool or
        heap allocation.

        @param bytes The number of bytes to allocate.

        @return A pointer to the allocated storage.

        @note The second (alignment) argument is ignored; only the
        default new alignment is honored. See the class-level note.
    */
    void*
    allocate_fast(std::size_t bytes, std::size_t /*alignment*/)
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

        @param p Pointer previously returned by `allocate_fast`
        (or `do_allocate`) on a resource that compares equal to this one.

        @param bytes The size, in bytes, originally requested for `p`.

        @note The third (alignment) argument is ignored; only the
        default new alignment is honored. See the class-level note.
    */
    void
    deallocate_fast(void* p, std::size_t bytes, std::size_t /*alignment*/)
    {
        std::size_t rounded = round_up_pow2(bytes);
        std::size_t idx = get_class_index(rounded);
        if(idx >= num_classes)
        {
            ::operator delete(p);
            return;
        }
        auto& lp = local();
        if(lp.buckets[idx].push(p))
            return;
        deallocate_slow(p, idx);
    }

protected:
    /** Allocate storage (`std::pmr::memory_resource` interface).

        Forwards to `allocate_fast`. The alignment argument is ignored;
        see the class-level note.

        @param bytes The number of bytes to allocate.

        @return A pointer to the allocated storage.
    */
    void*
    do_allocate(std::size_t bytes, std::size_t /*alignment*/) override;

    /** Deallocate storage (`std::pmr::memory_resource` interface).

        Forwards to `deallocate_fast`. The alignment argument is ignored;
        see the class-level note.

        @param p Pointer previously returned by `do_allocate`.

        @param bytes The size, in bytes, originally requested for `p`.
    */
    void
    do_deallocate(void* p, std::size_t bytes, std::size_t /*alignment*/) override;

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
