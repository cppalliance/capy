//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/recycling_memory_resource.hpp>

#include <cstddef>
#include <thread>
#include <vector>

#include "test_suite.hpp"

namespace boost {
namespace capy {

class recycling_memory_resource_test
{
public:
    void
    testIsEqual()
    {
        recycling_memory_resource a;
        recycling_memory_resource b;

        // Identity comparison only.
        BOOST_TEST(a.is_equal(a));
        BOOST_TEST(! a.is_equal(b));
    }

    void
    testSlowPaths()
    {
        // Drive enough same-class allocations/deallocations to exercise
        // the bucket overflow (deallocate past a full global pool frees
        // directly) and refill (allocate drains the global pool into the
        // thread-local one). Counts exceed 2*bucket_capacity (16) so the
        // slow paths run regardless of any prior pool state; only block
        // identity, which we do not assert, depends on that state.
        recycling_memory_resource mr;
        constexpr std::size_t bytes = 64;     // size class 0
        constexpr std::size_t align = 8;
        constexpr int n = 50;

        std::vector<void*> ptrs;
        ptrs.reserve(n);

        auto fill = [&] {
            for(int i = 0; i < n; ++i)
            {
                void* p = mr.allocate_fast(bytes, align);
                BOOST_TEST(p != nullptr);
                ptrs.push_back(p);
            }
        };
        auto drain = [&] {
            for(void* p : ptrs)
                mr.deallocate_fast(p, bytes, align);
            ptrs.clear();
        };

        fill();
        drain();   // overflows the global pool -> direct delete
        fill();    // drains the global pool back into the local one
        drain();
    }

    void
    testThreadExitTeardown()
    {
        // Each worker fills its thread-local pool with cached blocks and
        // then exits, running the pool's thread-exit drain. This is the
        // path that must survive the pool teardown being reached once
        // per exiting thread (and, on MinGW, potentially reached twice).
        // Repeat across many short-lived threads so a teardown fault
        // has ample opportunity to surface.
        constexpr int threads = 32;
        constexpr int rounds = 8;
        constexpr std::size_t bytes = 128; // size class 1
        constexpr std::size_t align = 8;

        for(int r = 0; r < rounds; ++r)
        {
            std::vector<std::thread> ts;
            ts.reserve(threads);
            for(int t = 0; t < threads; ++t)
            {
                ts.emplace_back([] {
                    recycling_memory_resource mr;
                    std::vector<void*> ptrs;
                    for(int i = 0; i < 40; ++i)
                        ptrs.push_back(mr.allocate_fast(bytes, align));
                    // Return them so the thread-local pool caches blocks
                    // that its exit-time drain must release.
                    for(void* p : ptrs)
                        mr.deallocate_fast(p, bytes, align);
                });
            }
            for(auto& t : ts)
                t.join();
        }

        BOOST_TEST_PASS();
    }

    void
    run()
    {
        testIsEqual();
        testSlowPaths();
        testThreadExitTeardown();
    }
};

TEST_SUITE(
    recycling_memory_resource_test,
    "boost.capy.recycling_memory_resource");

} // capy
} // boost
