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

#include <algorithm>
#include <barrier>
#include <cstddef>
#include <latch>
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
        // releasing what the pools can't keep and refilling (allocate
        // drains the global pool into the thread-local one), in every
        // size class. Counts exceed twice the class capacity, so neither
        // pool can keep every block whatever its prior state; only block
        // identity, which we do not assert, depends on that state.
        recycling_memory_resource mr;
        constexpr std::size_t align = 8;

        for(std::size_t bytes = 64; bytes <= 2048; bytes *= 2)
        {
            std::size_t const n = 2 * (256 * 1024 / bytes) + 44;

            std::vector<void*> ptrs;
            ptrs.reserve(n);

            auto fill = [&] {
                for(std::size_t i = 0; i < n; ++i)
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
            drain();   // more than both pools hold: the excess is released
            fill();    // drains the global pool back into the local one
            drain();
        }
    }

    // A fan-out keeps hundreds of same-sized frames alive at once. Once
    // they are freed, a thread must hand every one of them back instead
    // of falling through to the upstream allocator.
    void
    testRecyclesWideFanOut()
    {
        constexpr std::size_t bytes = 512; // size class 3
        constexpr std::size_t align = 8;
        constexpr int n = 256;

        std::thread([] {
            recycling_memory_resource mr;
            std::vector<void*> first;
            first.reserve(n);
            for(int i = 0; i < n; ++i)
                first.push_back(mr.allocate_fast(bytes, align));
            for(void* p : first)
                mr.deallocate_fast(p, bytes, align);

            std::vector<void*> second;
            second.reserve(n);
            for(int i = 0; i < n; ++i)
                second.push_back(mr.allocate_fast(bytes, align));

            // Reversed order makes it unlikely that the upstream allocator
            // handed the same addresses back after they were freed to it.
            BOOST_TEST(std::equal(
                second.begin(), second.end(), first.rbegin()));

            for(void* p : second)
                mr.deallocate_fast(p, bytes, align);
        }).join();
    }

    // Runs `owner` on a thread that keeps its pool alive while a second,
    // fresh thread takes every block the global pool can hand it, and
    // returns how many of the owner's freed blocks that thread received.
    template<class Owner>
    static std::size_t
    blocks_reaching_other_thread(Owner owner)
    {
        constexpr std::size_t bytes = 256; // size class 2
        constexpr std::size_t align = 8;
        constexpr int takes = 2048;        // twice the class capacity

        // Earlier tests may have filled the global pool, which would turn
        // the owner's decayed blocks away. A thread's first allocation
        // takes the whole global bucket, and its exit frees it.
        std::thread([] {
            recycling_memory_resource mr;
            mr.deallocate_fast(mr.allocate_fast(bytes, align), bytes, align);
        }).join();

        std::vector<void*> owned;
        std::size_t found = 0;
        std::latch ready(1);
        std::latch done(1);

        std::thread holder([&] {
            recycling_memory_resource mr;
            owned = owner(mr, bytes, align);
            ready.count_down();
            done.wait();
        });
        ready.wait();
        std::thread([&] {
            recycling_memory_resource mr;
            std::vector<void*> taken;
            for(int i = 0; i < takes; ++i)
                taken.push_back(mr.allocate_fast(bytes, align));
            for(void* p : taken)
                found += static_cast<std::size_t>(
                    std::count(owned.begin(), owned.end(), p));
            for(void* p : taken)
                mr.deallocate_fast(p, bytes, align);
        }).join();
        done.count_down();
        holder.join();
        return found;
    }

    // Allocates and frees a small block many times, enough for many
    // decay passes on the calling thread.
    static void
    churn(recycling_memory_resource& mr)
    {
        for(int i = 0; i < 1 << 18; ++i)
            mr.deallocate_fast(mr.allocate_fast(64, 8), 64, 8);
    }

    // A burst cached on one thread and then left idle must become
    // available to other threads instead of staying with the thread
    // that freed it.
    void
    testIdleBurstDecays()
    {
        constexpr int n = 64;
        auto found = blocks_reaching_other_thread(
            [](recycling_memory_resource& mr, std::size_t bytes,
               std::size_t align) {
                std::vector<void*> ptrs;
                for(int i = 0; i < n; ++i)
                    ptrs.push_back(mr.allocate_fast(bytes, align));
                for(void* p : ptrs)
                    mr.deallocate_fast(p, bytes, align);
                churn(mr);
                return ptrs;
            });
        BOOST_TEST_EQ(found, std::size_t(n));
    }

    // A burst the thread repeats keeps its blocks cached on that thread.
    void
    testRecurringBurstStaysCached()
    {
        constexpr int n = 64;
        auto found = blocks_reaching_other_thread(
            [](recycling_memory_resource& mr, std::size_t bytes,
               std::size_t align) {
                std::vector<void*> ptrs;
                for(int round = 0; round < 1 << 12; ++round)
                {
                    ptrs.clear();
                    for(int i = 0; i < n; ++i)
                        ptrs.push_back(mr.allocate_fast(bytes, align));
                    for(void* p : ptrs)
                        mr.deallocate_fast(p, bytes, align);
                }
                return ptrs;
            });
        BOOST_TEST_EQ(found, std::size_t(0));
    }

    // Idle blocks the full global pool has no room for are released,
    // not kept by the thread that cached them.
    void
    testIdleBurstIntoFullGlobalPool()
    {
        constexpr std::size_t bytes = 256; // size class 2
        constexpr std::size_t align = 8;
        constexpr int cap = 1024;          // class capacity
        constexpr int n = 64;

        std::vector<void*> burst;
        std::size_t found = 0;
        std::latch held(1);
        std::latch full(1);

        std::thread owner([&] {
            recycling_memory_resource mr;
            for(int i = 0; i < n; ++i)
                burst.push_back(mr.allocate_fast(bytes, align));
            for(void* p : burst)
                mr.deallocate_fast(p, bytes, align);
            held.count_down();
            full.wait();
            churn(mr);
            std::vector<void*> again;
            for(int i = 0; i < n; ++i)
                again.push_back(mr.allocate_fast(bytes, align));
            for(void* p : again)
                found += static_cast<std::size_t>(
                    std::count(burst.begin(), burst.end(), p));
            for(void* p : again)
                mr.deallocate_fast(p, bytes, align);
        });
        held.wait();

        // Fills this thread's pool, then the global one; the exit drain
        // empties only the former.
        std::thread([] {
            recycling_memory_resource mr;
            std::vector<void*> ptrs;
            for(int i = 0; i < 2 * cap + n; ++i)
                ptrs.push_back(mr.allocate_fast(bytes, align));
            for(void* p : ptrs)
                mr.deallocate_fast(p, bytes, align);
        }).join();
        full.count_down();
        owner.join();

        BOOST_TEST_EQ(found, std::size_t(0));
    }

    // Stocks the global pool's bucket for `bytes` from a thread that then
    // exits. Returns the blocks freed once that thread's own pool was
    // full; if no decay pass ran on it, they fill the whole bucket.
    static std::vector<void*>
    stock_global_pool(std::size_t bytes, int cap)
    {
        std::vector<void*> stocked;
        std::thread([&] {
            recycling_memory_resource mr;
            std::vector<void*> ptrs;
            for(int i = 0; i < 2 * cap; ++i)
                ptrs.push_back(mr.allocate_fast(bytes, 8));
            for(void* p : ptrs)
                mr.deallocate_fast(p, bytes, 8);
            stocked.assign(ptrs.begin() + cap, ptrs.end());
        }).join();
        return stocked;
    }

    // A thread that only allocates still hands back a refill it stopped
    // using, as long as it goes on refilling other size classes.
    void
    testAllocateOnlyThreadDecays()
    {
        constexpr std::size_t idle_bytes = 256; // size class 2
        constexpr int idle_cap = 1024;
        constexpr std::size_t busy_bytes = 64;  // size class 0
        constexpr int busy_cap = 4096;
        // A refill after the first is nearly a whole bucket, so a decay
        // pass runs about every second round. Two passes are needed, as
        // the first only marks the idle blocks; the rest is margin.
        constexpr int rounds = 8;

        auto stocked = stock_global_pool(idle_bytes, idle_cap);
        std::barrier<> sync(2);

        // Takes the whole stocked bucket with one allocation, then only
        // allocates from another class.
        std::thread owner([&] {
            recycling_memory_resource mr;
            void* idle = mr.allocate_fast(idle_bytes, 8);
            std::vector<void*> busy;
            for(int r = 0; r < rounds; ++r)
            {
                sync.arrive_and_wait();
                for(int i = 0; i < busy_cap; ++i)
                    busy.push_back(mr.allocate_fast(busy_bytes, 8));
                sync.arrive_and_wait();
            }
            sync.arrive_and_wait();
            mr.deallocate_fast(idle, idle_bytes, 8);
            for(void* p : busy)
                mr.deallocate_fast(p, busy_bytes, 8);
        });
        for(int r = 0; r < rounds; ++r)
        {
            stock_global_pool(busy_bytes, busy_cap);
            sync.arrive_and_wait();
            sync.arrive_and_wait();
        }

        std::size_t found = 0;
        std::thread([&] {
            recycling_memory_resource mr;
            std::vector<void*> taken;
            for(int i = 0; i < idle_cap; ++i)
                taken.push_back(mr.allocate_fast(idle_bytes, 8));
            for(void* p : taken)
                found += static_cast<std::size_t>(
                    std::count(stocked.begin(), stocked.end(), p));
            for(void* p : taken)
                mr.deallocate_fast(p, idle_bytes, 8);
        }).join();
        sync.arrive_and_wait();
        owner.join();

        BOOST_TEST(found > 0);
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

    // A thread_local made before the thread's pool was first used is
    // destroyed after that pool's exit-time drain. Blocks it allocates
    // and frees then are not left in the drained pool, where nothing
    // releases them.
    void
    testUseAfterThreadExitDrain()
    {
        constexpr std::size_t bytes = 128; // size class 1
        constexpr std::size_t align = 8;

        std::thread([] {
            struct late_user
            {
                ~late_user()
                {
                    recycling_memory_resource mr;
                    void* p = mr.allocate_fast(bytes, align);
                    mr.deallocate_fast(p, bytes, align);
                }
            };
            thread_local late_user user;

            // Cache blocks for the exit-time drain to release.
            recycling_memory_resource mr;
            std::vector<void*> ptrs;
            for(int i = 0; i < 40; ++i)
                ptrs.push_back(mr.allocate_fast(bytes, align));
            for(void* p : ptrs)
                mr.deallocate_fast(p, bytes, align);
        }).join();

        BOOST_TEST_PASS();
    }

    void
    run()
    {
        testIsEqual();
        testSlowPaths();
        testRecyclesWideFanOut();
        testIdleBurstDecays();
        testRecurringBurstStaysCached();
        testIdleBurstIntoFullGlobalPool();
        testAllocateOnlyThreadDecays();
        testThreadExitTeardown();
        testUseAfterThreadExitDrain();
    }
};

TEST_SUITE(
    recycling_memory_resource_test,
    "boost.capy.recycling_memory_resource");

} // capy
} // boost
