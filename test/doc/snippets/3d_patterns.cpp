//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/3.concurrency/3d.patterns.adoc.

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

// tag::thread_safe_queue[]
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
// end::thread_safe_queue[]

#include <future>

#include "test_suite.hpp"

namespace {

// tag::thread_safe_queue[]

template<typename T>
class ThreadSafeQueue
{
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;

public:
    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        cv_.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]{ return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }
};
// end::thread_safe_queue[]

// The page's std::async fragments launch this function.
int compute()
{
    return 6 * 7;
}

struct patterns_test
{
    void
    testLaunchPolicies()
    {
        {
            // tag::launch_policies[]
            // Force a new thread
            auto future = std::async(std::launch::async, compute);
            // end::launch_policies[]
            BOOST_TEST(future.get() == 42);
        }
        {
            // tag::launch_policies[]

            // Defer execution until get()
            auto future = std::async(std::launch::deferred, compute);
            // end::launch_policies[]
            BOOST_TEST(future.get() == 42);
        }
        {
            // tag::launch_policies[]

            // Let the system decide (default)
            auto future = std::async(std::launch::async | std::launch::deferred, compute);
            // end::launch_policies[]
            BOOST_TEST(future.get() == 42);
        }
    }

    void
    testQueue()
    {
        ThreadSafeQueue<int> queue;
        queue.push(1);
        queue.push(2);
        BOOST_TEST(queue.pop() == 1);
        BOOST_TEST(queue.pop() == 2);
    }

    void
    run()
    {
        testLaunchPolicies();
        testQueue();
    }
};

} // namespace

TEST_SUITE(patterns_test, "boost.capy.doc.3d_patterns");
