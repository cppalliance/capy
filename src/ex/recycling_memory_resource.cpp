//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/ex/recycling_memory_resource.hpp>

#include <new>
#include <tuple>

namespace boost {
namespace capy {

// Instance destruction does nothing: the resource is stateless (all pools
// are static). Global-pool cleanup is owned by global()'s holder, exactly
// as in the original where the global pool's own destructor drained it,
// independent of any instance lifetime.
recycling_memory_resource::~recycling_memory_resource() = default;

#if (defined(BOOST_CAPY_DYN_LINK) || defined(BOOST_ALL_DYN_LINK)) && \
    !defined(BOOST_CAPY_STATIC_LINK)

recycling_memory_resource::pool&
recycling_memory_resource::local() noexcept
{
    static thread_local pool p;
    return p;
}

bool&
recycling_memory_resource::thread_armed() noexcept
{
    static thread_local bool armed = false;
    return armed;
}

#endif

namespace {

// Set once this thread's pool was drained at exit. Another thread_local
// destroyed later can still free frames; they must not be cached.
thread_local bool pool_drained = false;

} // namespace

bool
recycling_memory_resource::arm_thread_cleanup() noexcept
{
    struct janitor
    {
        ~janitor()
        {
            // Return this thread's cached blocks to the OS so nothing
            // leaks at thread exit. The pool has a trivial dtor, so its
            // thread-local storage is still valid here.
            auto& lp = local();
            for(auto& b : lp.buckets)
                while(b.count > 0)
                    ::operator delete(b.pop());
            pool_drained = true;
            thread_armed() = false;
        }
    };
    if(pool_drained)
        return false;
    static thread_local janitor j;
    std::ignore = j;
    return true;
}

recycling_memory_resource::pool&
recycling_memory_resource::global() noexcept
{
    // Holder gives the global pool a destructor (the trivial-dtor pool type
    // itself must stay guard-free for local()). Runs unconditionally at
    // process exit, mirroring the original global pool destructor, and is
    // locked because a worker thread may still be in a slow path. This path
    // is cold (slow paths only), so the holder's guard costs nothing hot.
    struct holder
    {
        pool p;

        ~holder()
        {
            std::lock_guard<std::mutex> lock(global_mutex());
            for(auto& b : p.buckets)
                while(b.count > 0)
                    ::operator delete(b.pop());
        }
    };
    static holder h;
    return h.p;
}

std::mutex&
recycling_memory_resource::global_mutex() noexcept
{
    // Never destroyed: it is locked during process-exit teardown (in
    // global()'s holder destructor), after a function-local `static
    // std::mutex` could itself have been destroyed. Placement-new into
    // static storage owns no heap allocation, so there is nothing to leak.
    alignas(std::mutex) static unsigned char storage[sizeof(std::mutex)];
    static std::mutex* const mtx =
        ::new(static_cast<void*>(storage)) std::mutex();
    return *mtx;
}

void*
recycling_memory_resource::allocate_slow(
    std::size_t rounded, std::size_t idx)
{
    // Once this thread's pool was drained at exit, take one block and
    // leave the pool empty: nothing would release a refill.
    bool const cache = arm_thread_cleanup();
    {
        std::lock_guard<std::mutex> lock(global_mutex());
        auto& b = global().buckets[idx];
        if(auto* p = cache ? b.pop(local().buckets[idx]) : b.pop())
            return p;
    }
    return ::operator new(rounded);
}

void
recycling_memory_resource::deallocate_slow(
    void* p, std::size_t idx)
{
    {
        std::lock_guard<std::mutex> lock(global_mutex());
        if(global().buckets[idx].push(p))
            return;
    }
    ::operator delete(p);
}

void*
recycling_memory_resource::do_allocate(std::size_t bytes, std::size_t alignment)
{
    return allocate_fast(bytes, alignment);
}

void
recycling_memory_resource::do_deallocate(void* p, std::size_t bytes, std::size_t alignment)
{
    deallocate_fast(p, bytes, alignment);
}

std::pmr::memory_resource*
get_recycling_memory_resource() noexcept
{
    static recycling_memory_resource instance;
    return &instance;
}

} // namespace capy
} // namespace boost
