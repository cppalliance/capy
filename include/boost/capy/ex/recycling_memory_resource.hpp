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

#include <cstddef>
#include <memory_resource>
#include <mutex>

namespace boost {
namespace capy {

/** Recycling memory resource with thread-local and global pools.

    This memory resource recycles memory blocks to reduce allocation
    overhead for coroutine frames. It maintains a thread-local pool
    for fast lock-free access and a global pool for cross-thread
    block sharing.

    Blocks are tracked by size to avoid returning undersized blocks.

    This is the default allocator used by run_async when no allocator
    is specified.

    @par Thread Safety
    Thread-safe. The thread-local pool requires no synchronization.
    The global pool uses a mutex for cross-thread access.

    @see get_recycling_memory_resource
    @see run_async
*/
class recycling_memory_resource : public std::pmr::memory_resource
{
    struct block
    {
        block* next;
        std::size_t size;
    };

    struct global_pool
    {
        std::mutex mtx;
        block* head = nullptr;

        ~global_pool()
        {
            while(head)
            {
                auto p = head;
                head = head->next;
                ::operator delete(p);
            }
        }

        void push(block* b)
        {
            std::lock_guard<std::mutex> lock(mtx);
            b->next = head;
            head = b;
        }

        block* pop(std::size_t n)
        {
            std::lock_guard<std::mutex> lock(mtx);
            block** pp = &head;
            while(*pp)
            {
                if((*pp)->size >= n + sizeof(block))
                {
                    block* p = *pp;
                    *pp = p->next;
                    return p;
                }
                pp = &(*pp)->next;
            }
            return nullptr;
        }
    };

    struct local_pool
    {
        block* head = nullptr;

        ~local_pool()
        {
            while(head)
            {
                auto p = head;
                head = head->next;
                ::operator delete(p);
            }
        }

        void push(block* b)
        {
            b->next = head;
            head = b;
        }

        block* pop(std::size_t n)
        {
            block** pp = &head;
            while(*pp)
            {
                if((*pp)->size >= n + sizeof(block))
                {
                    block* p = *pp;
                    *pp = p->next;
                    return p;
                }
                pp = &(*pp)->next;
            }
            return nullptr;
        }
    };

    static local_pool& local()
    {
        static thread_local local_pool pool;
        return pool;
    }

    static global_pool& global()
    {
        static global_pool pool;
        return pool;
    }

protected:
    void*
    do_allocate(std::size_t bytes, std::size_t) override
    {
        std::size_t total = bytes + sizeof(block);

        if(auto* b = local().pop(bytes))
            return static_cast<char*>(static_cast<void*>(b)) + sizeof(block);

        if(auto* b = global().pop(bytes))
            return static_cast<char*>(static_cast<void*>(b)) + sizeof(block);

        auto* b = static_cast<block*>(::operator new(total));
        b->next = nullptr;
        b->size = total;
        return static_cast<char*>(static_cast<void*>(b)) + sizeof(block);
    }

    void
    do_deallocate(void* p, std::size_t, std::size_t) override
    {
        auto* b = static_cast<block*>(
            static_cast<void*>(static_cast<char*>(p) - sizeof(block)));
        b->next = nullptr;
        local().push(b);
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
inline std::pmr::memory_resource*
get_recycling_memory_resource() noexcept
{
    static recycling_memory_resource instance;
    return &instance;
}

} // namespace capy
} // namespace boost

#endif
