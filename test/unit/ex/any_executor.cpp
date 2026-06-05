//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/any_executor.hpp>

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace boost {
namespace capy {

namespace {

// Verify Executor concept at compile time
static_assert(Executor<any_executor>,
    "any_executor must satisfy Executor concept");

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

// Executor whose work-tracking hooks are observable, so tests can
// confirm on_work_started/on_work_finished forward through the wrapper.
struct counting_context : execution_context
{
    int work = 0;
};

struct counting_executor
{
    counting_context* ctx_ = nullptr;

    counting_executor() = default;
    explicit counting_executor(counting_context& ctx) noexcept : ctx_(&ctx) {}

    bool operator==(counting_executor const& other) const noexcept
    {
        return ctx_ == other.ctx_;
    }
    execution_context& context() const noexcept { return *ctx_; }
    void on_work_started() const noexcept { ++ctx_->work; }
    void on_work_finished() const noexcept { --ctx_->work; }
    std::coroutine_handle<> dispatch(continuation& c) const { return c.h; }
    void post(continuation&) const { }
};

static_assert(Executor<counting_executor>);

} // namespace

struct any_executor_test
{
    void
    testConstruct()
    {
        // Default construct
        {
            any_executor ex;
            BOOST_TEST(!ex);
        }

        // Construct from executor
        {
            thread_pool pool(1);
            auto executor = pool.get_executor();
            any_executor ex(executor);
            BOOST_TEST(static_cast<bool>(ex));
        }
    }

    void
    testCopy()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();
        any_executor ex1(executor);

        // Copy construction
        auto ex2 = ex1;
        BOOST_TEST(ex1 == ex2);

        // Copy assignment
        any_executor ex3;
        ex3 = ex1;
        BOOST_TEST(ex1 == ex3);
    }

    void
    testMove()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();
        any_executor ex1(executor);

        // Move construction - source should remain valid
        any_executor ex2(std::move(ex1));
        BOOST_TEST(static_cast<bool>(ex2));
        BOOST_TEST(static_cast<bool>(ex1)); // No moved-from state
        BOOST_TEST(ex1 == ex2);

        // Move assignment - source should remain valid
        any_executor ex3;
        ex3 = std::move(ex2);
        BOOST_TEST(static_cast<bool>(ex3));
        BOOST_TEST(static_cast<bool>(ex2)); // No moved-from state
        BOOST_TEST(ex2 == ex3);
    }

    void
    testEquality()
    {
        thread_pool pool1(1);
        thread_pool pool2(1);
        auto executor1 = pool1.get_executor();
        auto executor2 = pool2.get_executor();

        any_executor ex1(executor1);
        any_executor ex2(executor1);  // Same underlying executor
        any_executor ex3(executor2);  // Different underlying executor
        any_executor ex4;             // Empty

        BOOST_TEST(ex1 == ex2);
        BOOST_TEST(!(ex1 == ex3));

        // Empty comparisons
        any_executor ex5;
        BOOST_TEST(ex4 == ex5);       // Both empty
        BOOST_TEST(!(ex1 == ex4));    // Non-empty vs empty
    }

    void
    testWorkTracking()
    {
        // on_work_started/on_work_finished forward through the
        // type-erased impl to the wrapped executor.
        counting_context ctx;
        counting_executor under(ctx);
        any_executor ex(under);

        BOOST_TEST_EQ(ctx.work, 0);
        ex.on_work_started();
        BOOST_TEST_EQ(ctx.work, 1);
        ex.on_work_finished();
        BOOST_TEST_EQ(ctx.work, 0);
    }

    void
    testTargetType()
    {
        // Empty executor
        {
            any_executor ex;
            BOOST_TEST(ex.target_type() == typeid(void));
        }

        // With executor
        {
            thread_pool pool(1);
            any_executor ex(pool.get_executor());
            BOOST_TEST(ex.target_type() == typeid(thread_pool::executor_type));
        }
    }

    void
    testContext()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();
        any_executor ex(executor);

        // context() should return the same reference as the underlying executor
        BOOST_TEST(&ex.context() == &executor.context());
    }

    void
    testDispatch()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();
        any_executor ex(executor);

        std::atomic<int> counter{0};
        auto coro = make_counter_coro(counter);
        continuation c{coro.handle()};
        ex.dispatch(c);
        coro.release();

        BOOST_TEST(wait_for([&]{ return counter.load() >= 1; }));
        BOOST_TEST_EQ(counter.load(), 1);
    }

    void
    testPost()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();
        any_executor ex(executor);

        std::atomic<int> counter{0};
        auto coro = make_counter_coro(counter);
        continuation c{coro.handle()};
        ex.post(c);
        coro.release();

        BOOST_TEST(wait_for([&]{ return counter.load() >= 1; }));
        BOOST_TEST_EQ(counter.load(), 1);
    }

    void
    testMultiplePost()
    {
        std::atomic<int> counter{0};
        constexpr int N = 10;

        // continuations must outlive pool to avoid
        // dangling pointers in the executor queue.
        counter_coro coros[N] = {
            make_counter_coro(counter),
            make_counter_coro(counter),
            make_counter_coro(counter),
            make_counter_coro(counter),
            make_counter_coro(counter),
            make_counter_coro(counter),
            make_counter_coro(counter),
            make_counter_coro(counter),
            make_counter_coro(counter),
            make_counter_coro(counter),
        };
        continuation conts[N] = {};

        thread_pool pool(2);
        auto executor = pool.get_executor();
        any_executor ex(executor);

        for(int i = 0; i < N; ++i)
        {
            conts[i] = continuation{coros[i].handle()};
            ex.post(conts[i]);
            coros[i].release();
        }

        BOOST_TEST(wait_for([&]{ return counter.load() >= N; }));
        BOOST_TEST_EQ(counter.load(), N);
    }

    void
    testSharedOwnership()
    {
        std::atomic<int> counter{0};

        // continuations must outlive pool to avoid
        // dangling pointers in the executor queue.
        auto coro1 = make_counter_coro(counter);
        auto coro2 = make_counter_coro(counter);
        auto coro3 = make_counter_coro(counter);
        continuation c1{coro1.handle()};
        continuation c2{coro2.handle()};
        continuation c3{coro3.handle()};

        thread_pool pool(1);
        auto executor = pool.get_executor();

        // Create any_executor and make copies
        any_executor ex1(executor);
        any_executor ex2 = ex1;
        any_executor ex3 = ex1;

        // All should be equal
        BOOST_TEST(ex1 == ex2);
        BOOST_TEST(ex2 == ex3);

        // Post through different copies
        ex1.post(c1);
        coro1.release();
        ex2.post(c2);
        coro2.release();
        ex3.post(c3);
        coro3.release();

        BOOST_TEST(wait_for([&]{ return counter.load() >= 3; }));
        BOOST_TEST_EQ(counter.load(), 3);
    }

    void
    run()
    {
        testConstruct();
        testCopy();
        testMove();
        testEquality();
        testWorkTracking();
        testTargetType();
        testContext();
        testDispatch();
        testPost();
        testMultiplePost();
        testSharedOwnership();
    }
};

TEST_SUITE(
    any_executor_test,
    "boost.capy.any_executor");

} // capy
} // boost
