//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/ex/recycling_memory_resource.hpp>

namespace boost {
namespace capy {

recycling_memory_resource::~recycling_memory_resource() = default;

recycling_memory_resource::pool&
recycling_memory_resource::global() noexcept
{
    static pool p;
    return p;
}

std::mutex&
recycling_memory_resource::global_mutex() noexcept
{
    static std::mutex mtx;
    return mtx;
}

void*
recycling_memory_resource::allocate_slow(
    std::size_t rounded, std::size_t idx)
{
    {
        std::lock_guard<std::mutex> lock(global_mutex());
        if(auto* p = global().buckets[idx].pop(local().buckets[idx]))
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
