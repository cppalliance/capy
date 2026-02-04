//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
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
class recycling_memory_resource : public std::pmr::memory_resource
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

    struct pool
    {
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

            bool push(void* p) noexcept
            {
                if(count >= bucket_capacity)
                    return false;
                ptrs[count++] = p;
                return true;
            }
        };

        bucket buckets[num_classes];

        ~pool()
        {
            for(auto& b : buckets)
                while(b.count > 0)
                    ::operator delete(b.pop());
        }
    };

    static pool&
    local() noexcept
    {
        static thread_local pool p;
        return p;
    }

    static pool&
    global() noexcept
    {
        static pool p;
        return p;
    }

    static std::mutex&
    global_mutex() noexcept
    {
        static std::mutex mtx;
        return mtx;
    }

protected:
    void*
    do_allocate(std::size_t bytes, std::size_t) override
    {
        std::size_t rounded = round_up_pow2(bytes);
        std::size_t idx = get_class_index(rounded);

        if(idx >= num_classes)
            return ::operator new(bytes);

        if(auto* p = local().buckets[idx].pop())
            return p;

        {
            std::lock_guard<std::mutex> lock(global_mutex());
            if(auto* p = global().buckets[idx].pop())
                return p;
        }

        return ::operator new(rounded);
    }

    void
    do_deallocate(void* p, std::size_t bytes, std::size_t) override
    {
        std::size_t rounded = round_up_pow2(bytes);
        std::size_t idx = get_class_index(rounded);

        if(idx >= num_classes)
        {
            ::operator delete(p);
            return;
        }

        if(local().buckets[idx].push(p))
            return;

        {
            std::lock_guard<std::mutex> lock(global_mutex());
            if(global().buckets[idx].push(p))
                return;
        }

        ::operator delete(p);
    }

    bool
    do_is_equal(const memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

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
