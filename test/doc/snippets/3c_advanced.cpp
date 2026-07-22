//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/3.concurrency/3c.advanced.adoc.

// tag::shared_mutex[]
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
#include <thread>
#include <shared_mutex>
#include <vector>
// end::shared_mutex[]

// tag::thread_safe_cache[]
#include <shared_mutex>
#include <unordered_map>
#include <string>
#include <optional>
// end::thread_safe_cache[]

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "test_suite.hpp"

namespace {

// tag::shared_mutex[]

std::shared_mutex rw_mutex;
std::vector<int> data;

void reader(int id)
{
    std::shared_lock<std::shared_mutex> lock(rw_mutex);  // shared access
    std::cout << "Reader " << id << " sees " << data.size() << " elements\n";
}

void writer(int value)
{
    std::unique_lock<std::shared_mutex> lock(rw_mutex);  // exclusive access
    data.push_back(value);
    std::cout << "Writer added " << value << "\n";
}
// end::shared_mutex[]

// tag::thread_safe_cache[]

class ThreadSafeCache
{
    std::unordered_map<std::string, std::string> cache_;
    mutable std::shared_mutex mutex_;

public:
    std::optional<std::string> get(std::string const& key) const
    {
        std::shared_lock lock(mutex_);  // readers can proceed in parallel
        auto it = cache_.find(key);
        if (it != cache_.end())
            return it->second;
        return std::nullopt;
    }

    void put(std::string const& key, std::string const& value)
    {
        std::unique_lock lock(mutex_);  // exclusive access for writing
        cache_[key] = value;
    }
};
// end::thread_safe_cache[]

// Shows the inefficient polling baseline the page contrasts with condition
// variables. Compile-only: calling it would spin until another thread
// flips `ready`.
[[maybe_unused]] void
busy_wait_demo(bool& ready)
{
    // tag::busy_wait[]
    // Inefficient busy-wait
    while (!ready)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // end::busy_wait[]
}

struct advanced_test
{
    void
    testAtomicOps()
    {
        // tag::atomic_ops[]
        std::atomic<int> value{0};

        value.store(42);              // atomic write
        int x = value.load();         // atomic read
        int old = value.exchange(10); // atomic read-modify-write
        value.fetch_add(5);           // atomic addition, returns old value
        value.fetch_sub(3);           // atomic subtraction, returns old value

        // Compare-and-swap (CAS)
        int expected = 10;
        bool success = value.compare_exchange_strong(expected, 20);
        // If value == expected, sets value = 20 and returns true
        // Otherwise, sets expected = value and returns false
        // end::atomic_ops[]
        BOOST_TEST(x == 42);
        BOOST_TEST(old == 42);
        BOOST_TEST(value.load() == 12);
        BOOST_TEST(!success);
        BOOST_TEST(expected == 12);
    }

    void
    testWaitVariants()
    {
        std::condition_variable cv;
        std::mutex m;
        std::unique_lock<std::mutex> lock(m);
        // Predicate is already true, so no variant blocks and no
        // notifier thread is needed.
        auto predicate = []{ return true; };
        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(1);
        {
            // tag::wait_variants[]
            // Wait indefinitely
            cv.wait(lock, predicate);

            // Wait with timeout
            auto status = cv.wait_for(lock, std::chrono::seconds(5), predicate);
            // Returns true if predicate is true, false on timeout
            // end::wait_variants[]
            BOOST_TEST(status);
        }
        {
            // tag::wait_variants[]

            // Wait until specific time point
            auto status = cv.wait_until(lock, deadline, predicate);
            // end::wait_variants[]
            BOOST_TEST(status);
        }
    }

    void
    testSharedMutex()
    {
        writer(7);
        reader(1);
        BOOST_TEST(data.size() == 1);
        BOOST_TEST(data.front() == 7);
    }

    void
    testCache()
    {
        ThreadSafeCache cache;
        BOOST_TEST(!cache.get("answer").has_value());
        cache.put("answer", "42");
        auto value = cache.get("answer");
        BOOST_TEST(value.has_value());
        BOOST_TEST(*value == "42");
    }

    void
    run()
    {
        testAtomicOps();
        testWaitVariants();
        testSharedMutex();
        testCache();
    }
};

} // namespace

TEST_SUITE(advanced_test, "boost.capy.doc.3c_advanced");
