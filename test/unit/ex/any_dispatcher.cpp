//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/any_dispatcher.hpp>

#include <boost/capy/concept/dispatcher.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace boost {
namespace capy {

namespace {

// Verify dispatcher concept at compile time
static_assert(dispatcher<any_dispatcher>,
    "any_dispatcher must satisfy dispatcher concept");
static_assert(dispatcher<any_post_dispatcher>,
    "any_post_dispatcher must satisfy dispatcher concept");
static_assert(dispatcher<any_defer_dispatcher>,
    "any_defer_dispatcher must satisfy dispatcher concept");
static_assert(dispatcher<post_dispatcher<thread_pool::executor_type>>,
    "post_dispatcher must satisfy dispatcher concept");
static_assert(dispatcher<defer_dispatcher<thread_pool::executor_type>>,
    "defer_dispatcher must satisfy dispatcher concept");

// Helper to wait for a condition with timeout
template<class Pred>
bool wait_for(Pred pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
{
    auto start = std::chrono::steady_clock::now();
    while(!pred())
    {
        if(std::chrono::steady_clock::now() - start > timeout)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// Simple test coroutine that increments a counter
struct counter_coro
{
    struct promise_type
    {
        std::atomic<int>* counter;

        counter_coro
        get_return_object() noexcept
        {
            return counter_coro{std::coroutine_handle<promise_type>::from_promise(*this)};
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

    ~counter_coro()
    {
        if(h_)
            h_.destroy();
    }

    counter_coro(counter_coro&& other) noexcept
        : h_(other.h_)
    {
        other.h_ = nullptr;
    }

    counter_coro& operator=(counter_coro&& other) noexcept
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
    explicit counter_coro(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
};

// Creates a coroutine that increments counter
inline counter_coro
make_counter_coro(std::atomic<int>& counter)
{
    return [](std::atomic<int>* counter) -> counter_coro {
        ++(*counter);
        co_return;
    }(&counter);
}

} // namespace

struct any_dispatcher_test
{
    void
    testConstruct()
    {
        // Default construct
        {
            any_dispatcher d;
            BOOST_TEST(!d);
        }

        // Construct from post_dispatcher
        {
            thread_pool pool(1);
            auto pd = post_dispatcher(pool.get_executor());
            any_dispatcher d(pd);
            BOOST_TEST(static_cast<bool>(d));
        }

        // Construct from defer_dispatcher
        {
            thread_pool pool(1);
            auto dd = defer_dispatcher(pool.get_executor());
            any_dispatcher d(dd);
            BOOST_TEST(static_cast<bool>(d));
        }
    }

    void
    testCopy()
    {
        thread_pool pool(1);
        auto pd = post_dispatcher(pool.get_executor());
        any_dispatcher d1(pd);

        // Copy construction
        auto d2 = d1;
        BOOST_TEST(d1 == d2);

        // Copy assignment
        any_dispatcher d3;
        d3 = d1;
        BOOST_TEST(d1 == d3);
    }

    void
    testEquality()
    {
        thread_pool pool(1);
        auto pd1 = post_dispatcher(pool.get_executor());
        auto pd2 = post_dispatcher(pool.get_executor());

        any_dispatcher d1(pd1);
        any_dispatcher d2(pd1);  // Same underlying dispatcher
        any_dispatcher d3(pd2);  // Different underlying dispatcher

        BOOST_TEST(d1 == d2);
        BOOST_TEST(!(d1 == d3));
    }

    void
    testDispatch()
    {
        thread_pool pool(1);
        auto pd = post_dispatcher(pool.get_executor());
        any_dispatcher d(pd);

        std::atomic<int> counter{0};
        auto coro = make_counter_coro(counter);
        d(coro.handle());
        coro.release();

        BOOST_TEST(wait_for([&]{ return counter.load() >= 1; }));
        BOOST_TEST_EQ(counter.load(), 1);
    }

    void
    run()
    {
        testConstruct();
        testCopy();
        testEquality();
        testDispatch();
    }
};

struct any_post_dispatcher_test
{
    void
    testConstruct()
    {
        // Default construct
        {
            any_post_dispatcher d;
            BOOST_TEST(!d);
        }

        // Construct from executor
        {
            thread_pool pool(1);
            auto ex = pool.get_executor();
            any_post_dispatcher d(ex);
            BOOST_TEST(static_cast<bool>(d));
        }
    }

    void
    testCopy()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();
        any_post_dispatcher d1(ex);

        // Copy construction
        auto d2 = d1;
        (void)d2;

        // Copy assignment
        any_post_dispatcher d3;
        d3 = d1;
        (void)d3;
    }

    void
    testDispatch()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();
        any_post_dispatcher d(ex);

        std::atomic<int> counter{0};
        auto coro = make_counter_coro(counter);
        auto result = d(coro.handle());
        coro.release();

        // post always returns noop_coroutine
        BOOST_TEST(result == std::noop_coroutine());

        BOOST_TEST(wait_for([&]{ return counter.load() >= 1; }));
        BOOST_TEST_EQ(counter.load(), 1);
    }

    void
    testMultipleDispatch()
    {
        thread_pool pool(2);
        auto ex = pool.get_executor();
        any_post_dispatcher d(ex);

        std::atomic<int> counter{0};
        constexpr int N = 10;

        for(int i = 0; i < N; ++i)
        {
            auto coro = make_counter_coro(counter);
            d(coro.handle());
            coro.release();
        }

        BOOST_TEST(wait_for([&]{ return counter.load() >= N; }));
        BOOST_TEST_EQ(counter.load(), N);
    }

    void
    run()
    {
        testConstruct();
        testCopy();
        testDispatch();
        testMultipleDispatch();
    }
};

struct any_defer_dispatcher_test
{
    void
    testConstruct()
    {
        // Default construct
        {
            any_defer_dispatcher d;
            BOOST_TEST(!d);
        }

        // Construct from executor
        {
            thread_pool pool(1);
            auto ex = pool.get_executor();
            any_defer_dispatcher d(ex);
            BOOST_TEST(static_cast<bool>(d));
        }
    }

    void
    testCopy()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();
        any_defer_dispatcher d1(ex);

        // Copy construction
        auto d2 = d1;
        (void)d2;

        // Copy assignment
        any_defer_dispatcher d3;
        d3 = d1;
        (void)d3;
    }

    void
    testDispatch()
    {
        thread_pool pool(1);
        auto ex = pool.get_executor();
        any_defer_dispatcher d(ex);

        std::atomic<int> counter{0};
        auto coro = make_counter_coro(counter);
        auto result = d(coro.handle());
        coro.release();

        // defer always returns noop_coroutine
        BOOST_TEST(result == std::noop_coroutine());

        BOOST_TEST(wait_for([&]{ return counter.load() >= 1; }));
        BOOST_TEST_EQ(counter.load(), 1);
    }

    void
    testMultipleDispatch()
    {
        thread_pool pool(2);
        auto ex = pool.get_executor();
        any_defer_dispatcher d(ex);

        std::atomic<int> counter{0};
        constexpr int N = 10;

        for(int i = 0; i < N; ++i)
        {
            auto coro = make_counter_coro(counter);
            d(coro.handle());
            coro.release();
        }

        BOOST_TEST(wait_for([&]{ return counter.load() >= N; }));
        BOOST_TEST_EQ(counter.load(), N);
    }

    void
    run()
    {
        testConstruct();
        testCopy();
        testDispatch();
        testMultipleDispatch();
    }
};

TEST_SUITE(
    any_dispatcher_test,
    "boost.capy.any_dispatcher");

TEST_SUITE(
    any_post_dispatcher_test,
    "boost.capy.any_post_dispatcher");

TEST_SUITE(
    any_defer_dispatcher_test,
    "boost.capy.any_defer_dispatcher");

} // capy
} // boost
