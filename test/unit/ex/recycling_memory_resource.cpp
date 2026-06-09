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
    run()
    {
        testIsEqual();
        testSlowPaths();
    }
};

TEST_SUITE(
    recycling_memory_resource_test,
    "boost.capy.recycling_memory_resource");

} // capy
} // boost
