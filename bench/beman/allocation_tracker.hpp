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
#include <memory_resource>
#include <new>

static std::atomic<int64_t> g_alloc_count{0};

/// Counts every allocate call, then delegates to upstream.
class counting_memory_resource
    : public std::pmr::memory_resource
{
    std::pmr::memory_resource* upstream_;

    void* do_allocate(
        std::size_t n, std::size_t align) override
    {
        g_alloc_count.fetch_add(1, std::memory_order_relaxed);
        return upstream_->allocate(n, align);
    }

    void do_deallocate(
        void* p, std::size_t n, std::size_t align) override
    {
        upstream_->deallocate(p, n, align);
    }

    bool do_is_equal(
        memory_resource const& other) const noexcept override
    {
        return this == &other;
    }

public:
    explicit counting_memory_resource(
        std::pmr::memory_resource* upstream) noexcept
        : upstream_(upstream) {}
};

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
