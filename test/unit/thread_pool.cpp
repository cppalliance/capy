//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/thread_pool.hpp>

#include "test_suite.hpp"

#include <boost/system/result.hpp>
#include <atomic>
#include <chrono>
#include <exception>
#include <thread>

namespace boost {
namespace capy {

struct thread_pool_test
{
    void
    testConstruct()
    {
        // Default construction (hardware concurrency)
        {
            thread_pool pool;
            BOOST_TEST(static_cast<bool>(pool.get_executor()));
        }

        // Explicit thread count
        {
            thread_pool pool(2);
            BOOST_TEST(static_cast<bool>(pool.get_executor()));
        }

        // Single thread
        {
            thread_pool pool(1);
            BOOST_TEST(static_cast<bool>(pool.get_executor()));
        }
    }

    void
    testGetExecutor()
    {
        thread_pool pool(1);
        executor exec1 = pool.get_executor();
        executor exec2 = pool.get_executor();

        BOOST_TEST(static_cast<bool>(exec1));
        BOOST_TEST(static_cast<bool>(exec2));
    }

    void
    testPostSingle()
    {
        std::atomic<bool> called{false};
        {
            thread_pool pool(1);
            executor exec = pool.get_executor();
            exec.post([&called]{ called = true; });
        }
        // Pool destructor waits for work to complete
        BOOST_TEST(called.load());
    }

    void
    testPostMultiple()
    {
        std::atomic<int> count{0};
        {
            thread_pool pool(2);
            executor exec = pool.get_executor();
            exec.post([&count]{ ++count; });
            exec.post([&count]{ ++count; });
            exec.post([&count]{ ++count; });
        }
        BOOST_TEST_EQ(count.load(), 3);
    }

    void
    testPostFromMultipleThreads()
    {
        std::atomic<int> count{0};
        {
            thread_pool pool(4);
            executor exec = pool.get_executor();

            std::thread t1([&]{
                for(int i = 0; i < 10; ++i)
                    exec.post([&count]{ ++count; });
            });

            std::thread t2([&]{
                for(int i = 0; i < 10; ++i)
                    exec.post([&count]{ ++count; });
            });

            t1.join();
            t2.join();
        }
        BOOST_TEST_EQ(count.load(), 20);
    }

    void
    testConcurrentExecution()
    {
        // Verify work runs on multiple threads concurrently
        std::atomic<int> concurrent{0};
        std::atomic<int> max_concurrent{0};

        {
            thread_pool pool(4);
            executor exec = pool.get_executor();

            for(int i = 0; i < 8; ++i)
            {
                exec.post([&]{
                    int c = ++concurrent;
                    // Update max if this is higher
                    int expected = max_concurrent.load();
                    while(c > expected)
                    {
                        if(max_concurrent.compare_exchange_weak(expected, c))
                            break;
                    }
                    // Simulate some work
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(10));
                    --concurrent;
                });
            }
        }

        // Should have had multiple concurrent executions
        BOOST_TEST(max_concurrent.load() > 1);
    }

    void
    testAsyncPost()
    {
        std::atomic<int> result{0};
        std::atomic<bool> handler_called{false};

        {
            thread_pool pool(1);
            executor exec = pool.get_executor();
            exec.async_post(
                []{ return 42; },
                [&](system::result<int, std::exception_ptr> r)
                {
                    if(r.has_value())
                        result = r.value();
                    handler_called = true;
                });
        }

        BOOST_TEST(handler_called.load());
        BOOST_TEST_EQ(result.load(), 42);
    }

    void
    testAsyncPostVoid()
    {
        std::atomic<bool> work_called{false};
        std::atomic<bool> handler_called{false};

        {
            thread_pool pool(1);
            executor exec = pool.get_executor();
            exec.async_post(
                [&work_called]{ work_called = true; },
                [&handler_called](system::result<void, std::exception_ptr>)
                {
                    handler_called = true;
                });
        }

        BOOST_TEST(work_called.load());
        BOOST_TEST(handler_called.load());
    }

    void
    testMultipleExecutors()
    {
        std::atomic<int> count{0};
        {
            thread_pool pool(2);
            executor exec1 = pool.get_executor();
            executor exec2 = pool.get_executor();
            executor exec3 = exec1;

            exec1.post([&count]{ ++count; });
            exec2.post([&count]{ ++count; });
            exec3.post([&count]{ ++count; });
        }
        BOOST_TEST_EQ(count.load(), 3);
    }

    void
    testDestructorWaitsForWork()
    {
        std::atomic<bool> started{false};
        std::atomic<bool> finished{false};

        {
            thread_pool pool(1);
            executor exec = pool.get_executor();
            exec.post([&]{
                started = true;
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(50));
                finished = true;
            });
            // Give work time to start
            while(!started.load())
                std::this_thread::yield();
        }
        // Pool destructor should have waited
        BOOST_TEST(finished.load());
    }

    void
    run()
    {
        testConstruct();
        testGetExecutor();
        testPostSingle();
        testPostMultiple();
        testPostFromMultipleThreads();
        testConcurrentExecution();
        testAsyncPost();
        testAsyncPostVoid();
        testMultipleExecutors();
        testDestructorWaitsForWork();
    }
};

TEST_SUITE(
    thread_pool_test,
    "boost.capy.thread_pool");

} // capy
} // boost
