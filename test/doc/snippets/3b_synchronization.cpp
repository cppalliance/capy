//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/3.concurrency/3b.synchronization.adoc.

// Fragments deliberately leave results and bindings unused; the pages
// explain the values in prose instead.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-function"
// gcc 15 with sanitizers misattributes coroutine frame delete paths
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
#if defined(_MSC_VER)
#pragma warning(disable: 4834) // discarding [[nodiscard]] return value
#pragma warning(disable: 4189) // local variable initialized but not referenced
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4101) // unreferenced local variable
#pragma warning(disable: 4456) // declaration hides previous local declaration
#pragma warning(disable: 4457) // declaration hides function parameter
#pragma warning(disable: 4458) // declaration hides class member
#pragma warning(disable: 4459) // declaration hides global declaration
#endif

#include <iostream>
#include <mutex>
#include <thread>

#include "test_suite.hpp"

namespace {

// The #include directives inside the tag expand to nothing here (the
// headers are already included above); they are kept for the page text.
// tag::lock_guard[]
#include <iostream>
#include <thread>
#include <mutex>

int counter = 0;
std::mutex counter_mutex;

void increment_many_times()
{
    for (int i = 0; i < 100000; ++i)
    {
        std::lock_guard<std::mutex> lock(counter_mutex);
        ++counter;
        // lock is automatically released when it goes out of scope
    }
}
// end::lock_guard[]

std::mutex some_mutex;

// tag::deadlock[]
std::mutex mutex1, mutex2;

void thread_a()
{
    std::lock_guard<std::mutex> lock1(mutex1);
    std::lock_guard<std::mutex> lock2(mutex2);  // blocks, waiting for B
    // ...
}

void thread_b()
{
    std::lock_guard<std::mutex> lock2(mutex2);
    std::lock_guard<std::mutex> lock1(mutex1);  // blocks, waiting for A
    // ...
}
// end::deadlock[]

// Compile-only bug demo: running thread_a and thread_b concurrently
// can deadlock, so nothing ever calls them.
[[maybe_unused]] void (* const deadlock_demo[])() = {thread_a, thread_b};

// tag::scoped_lock_multi[]
void safe_function()
{
    std::scoped_lock lock(mutex1, mutex2);  // locks both atomically
    // ...
}
// end::scoped_lock_multi[]

struct synchronization_test
{
    void
    testLockGuard()
    {
        std::thread t1(increment_many_times);
        std::thread t2(increment_many_times);
        t1.join();
        t2.join();
        BOOST_TEST(counter == 200000);
    }

    void
    testScopedLock()
    {
        // tag::scoped_lock[]
        std::scoped_lock lock(counter_mutex);  // C++17
        // end::scoped_lock[]
    }

    void
    testUniqueLock()
    {
        // tag::unique_lock[]
        std::unique_lock<std::mutex> lock(some_mutex, std::defer_lock);
        // mutex not yet locked

        lock.lock();  // lock when ready
        // ... do work ...
        lock.unlock();  // unlock early if needed
        // ... do other work ...
        // destructor unlocks again if still locked
        // end::unique_lock[]
        BOOST_TEST(!lock.owns_lock());
    }

    void
    testScopedLockMulti()
    {
        safe_function();
    }

    void
    run()
    {
        testLockGuard();
        testScopedLock();
        testUniqueLock();
        testScopedLockMulti();
    }
};

} // namespace

TEST_SUITE(synchronization_test, "boost.capy.doc.3b_synchronization");
