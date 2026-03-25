//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_ALLOCATION_TRACKER_HPP
#define BOOST_CAPY_BENCH_ALLOCATION_TRACKER_HPP

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

static std::atomic<int64_t> g_alloc_count{0};

void* operator new(std::size_t n)
{
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p)
        throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

#endif
