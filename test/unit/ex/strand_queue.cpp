//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test the strand_queue implementation detail.
// This header is in src/ as it's not part of the public API.
#include "../../../src/ex/detail/strand_queue.hpp"

#include "test_suite.hpp"

#include <coroutine>
#include <vector>

namespace boost {
namespace capy {
namespace detail {

// Simple test coroutine that records when it runs
struct test_coro
{
    struct promise_type
    {
        int* counter;
        std::vector<int>* log;
        int id;

        test_coro
        get_return_object() noexcept
        {
            return test_coro{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always
        initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_never
        final_suspend() noexcept
        {
            return {};
        }

        void
        return_void() noexcept
        {
        }

        void
        unhandled_exception()
        {
            std::terminate();
        }
    };

    std::coroutine_handle<promise_type> h_;

    ~test_coro()
    {
        if(h_)
            h_.destroy();
    }

    test_coro(test_coro&& other) noexcept
        : h_(other.h_)
    {
        other.h_ = nullptr;
    }

    test_coro& operator=(test_coro&& other) noexcept
    {
        if(h_)
            h_.destroy();
        h_ = other.h_;
        other.h_ = nullptr;
        return *this;
    }

    std::coroutine_handle<void>
    handle() const noexcept
    {
        return h_;
    }

    void
    release() noexcept
    {
        h_ = nullptr;
    }

private:
    explicit test_coro(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
};

// Creates a coroutine that increments counter and logs its id
inline test_coro
make_test_coro(int& counter, std::vector<int>& log, int id)
{
    // Access promise to store our tracking data
    auto result = []() -> test_coro { co_return; }();
    result.h_.promise().counter = &counter;
    result.h_.promise().log = &log;
    result.h_.promise().id = id;

    // Return a new coroutine that does the actual work
    return [](int* counter, std::vector<int>* log, int id) -> test_coro {
        ++(*counter);
        log->push_back(id);
        co_return;
    }(&counter, &log, id);
}

// Coroutine that pushes another coroutine during dispatch
inline test_coro
make_pusher_coro(
    strand_queue& q,
    int& counter,
    std::vector<int>& log,
    int id,
    std::coroutine_handle<void> to_push)
{
    return [](strand_queue* q, int* counter, std::vector<int>* log, int id,
        std::coroutine_handle<void> to_push) -> test_coro {
        ++(*counter);
        log->push_back(id);
        if(to_push)
            q->push(to_push);
        co_return;
    }(&q, &counter, &log, id, to_push);
}

struct strand_queue_test
{
    void
    testEmpty()
    {
        // Default constructed queue is empty
        strand_queue q;
        BOOST_TEST(q.empty());
    }

    void
    testSinglePushDispatch()
    {
        // Push and dispatch single coroutine
        strand_queue q;
        int counter = 0;
        std::vector<int> log;

        auto coro = make_test_coro(counter, log, 1);
        q.push(coro.handle());
        coro.release();

        BOOST_TEST(!q.empty());
        q.dispatch();
        BOOST_TEST(q.empty());
        BOOST_TEST_EQ(counter, 1);
        BOOST_TEST_EQ(log.size(), 1u);
        BOOST_TEST_EQ(log[0], 1);
    }

    void
    testFIFOOrder()
    {
        // Multiple coroutines dispatch in FIFO order
        strand_queue q;
        int counter = 0;
        std::vector<int> log;

        auto c1 = make_test_coro(counter, log, 1);
        auto c2 = make_test_coro(counter, log, 2);
        auto c3 = make_test_coro(counter, log, 3);

        q.push(c1.handle());
        q.push(c2.handle());
        q.push(c3.handle());
        c1.release();
        c2.release();
        c3.release();

        BOOST_TEST(!q.empty());
        q.dispatch();
        BOOST_TEST(q.empty());

        BOOST_TEST_EQ(counter, 3);
        BOOST_TEST_EQ(log.size(), 3u);
        BOOST_TEST_EQ(log[0], 1);
        BOOST_TEST_EQ(log[1], 2);
        BOOST_TEST_EQ(log[2], 3);
    }

    void
    testPushDuringDispatch()
    {
        // Coroutine can push new work during dispatch
        strand_queue q;
        int counter = 0;
        std::vector<int> log;

        // c2 will be pushed by c1 during dispatch
        auto c2 = make_test_coro(counter, log, 2);
        auto c1 = make_pusher_coro(q, counter, log, 1, c2.handle());
        c2.release();

        q.push(c1.handle());
        c1.release();

        q.dispatch();
        BOOST_TEST(q.empty());

        // Both should have run, c1 first then c2
        BOOST_TEST_EQ(counter, 2);
        BOOST_TEST_EQ(log.size(), 2u);
        BOOST_TEST_EQ(log[0], 1);
        BOOST_TEST_EQ(log[1], 2);
    }

    void
    testDispatchOnEmpty()
    {
        // Dispatch on empty queue is a no-op
        strand_queue q;
        BOOST_TEST(q.empty());
        q.dispatch();  // Should not crash
        BOOST_TEST(q.empty());
    }

    void
    testMultipleDispatchCycles()
    {
        // Multiple push/dispatch cycles reuse free list
        strand_queue q;
        int counter = 0;
        std::vector<int> log;

        // First cycle
        {
            auto c1 = make_test_coro(counter, log, 1);
            auto c2 = make_test_coro(counter, log, 2);
            q.push(c1.handle());
            q.push(c2.handle());
            c1.release();
            c2.release();
            q.dispatch();
        }

        BOOST_TEST_EQ(counter, 2);
        BOOST_TEST(q.empty());

        // Second cycle - should reuse frames from free list
        {
            auto c3 = make_test_coro(counter, log, 3);
            auto c4 = make_test_coro(counter, log, 4);
            q.push(c3.handle());
            q.push(c4.handle());
            c3.release();
            c4.release();
            q.dispatch();
        }

        BOOST_TEST_EQ(counter, 4);
        BOOST_TEST(q.empty());
        BOOST_TEST_EQ(log.size(), 4u);
    }

    void
    testDestructorCleansPending()
    {
        // Destructor cleans up pending work without resuming
        int counter = 0;
        std::vector<int> log;

        {
            strand_queue q;
            auto c1 = make_test_coro(counter, log, 1);
            auto c2 = make_test_coro(counter, log, 2);
            q.push(c1.handle());
            q.push(c2.handle());
            // Don't release - let test_coro destructors clean up
            // since dispatch() is never called
            // q destroyed here without dispatch
        }

        // Coroutines should not have run
        BOOST_TEST_EQ(counter, 0);
        BOOST_TEST(log.empty());
    }

    void
    testManyOperations()
    {
        // Stress test with many operations
        strand_queue q;
        int counter = 0;
        std::vector<int> log;
        constexpr int N = 100;

        std::vector<test_coro> coros;
        coros.reserve(N);

        for(int i = 0; i < N; ++i)
        {
            coros.push_back(make_test_coro(counter, log, i));
            q.push(coros.back().handle());
            coros.back().release();
        }

        q.dispatch();

        BOOST_TEST_EQ(counter, N);
        BOOST_TEST_EQ(log.size(), static_cast<std::size_t>(N));
        for(int i = 0; i < N; ++i)
            BOOST_TEST_EQ(log[i], i);
    }

    void
    run()
    {
        testEmpty();
        testSinglePushDispatch();
        testFIFOOrder();
        testPushDuringDispatch();
        testDispatchOnEmpty();
        testMultipleDispatchCycles();
        testDestructorCleansPending();
        testManyOperations();
    }
};

TEST_SUITE(
    strand_queue_test,
    "boost.capy.ex.detail.strand_queue");

} // namespace detail
} // namespace capy
} // namespace boost
