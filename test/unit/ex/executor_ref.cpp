//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/executor_ref.hpp>

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace boost {
namespace capy {

namespace {

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

struct executor_ref_test
{
    void
    testConstruct()
    {
        // Default construct
        {
            executor_ref ex;
            BOOST_TEST(!ex);
        }

        // Construct from executor
        {
            thread_pool pool(1);
            auto executor = pool.get_executor();
            executor_ref ex(executor);
            BOOST_TEST(static_cast<bool>(ex));
        }
    }

    void
    testCopy()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();
        executor_ref ex1(executor);

        // Copy construction
        auto ex2 = ex1;
        BOOST_TEST(ex1 == ex2);

        // Copy assignment
        executor_ref ex3;
        ex3 = ex1;
        BOOST_TEST(ex1 == ex3);
    }

    void
    testEquality()
    {
        thread_pool pool1(1);
        thread_pool pool2(1);
        auto executor1 = pool1.get_executor();
        auto executor2 = pool2.get_executor();

        executor_ref ex1(executor1);
        executor_ref ex2(executor1);  // Same underlying executor
        executor_ref ex3(executor2);  // Different underlying executor

        BOOST_TEST(ex1 == ex2);
        BOOST_TEST(!(ex1 == ex3));

        // Different executor types compare unequal via the vtable
        // mismatch path, without invoking the per-type equals thunk.
        test::blocking_context bctx;
        auto ie = bctx.get_executor();
        executor_ref ex4(ie);
        BOOST_TEST(!(ex1 == ex4));
    }

    void
    testWorkTracking()
    {
        // on_work_started/on_work_finished forward through the vtable
        // thunks to the wrapped executor.
        counting_context ctx;
        counting_executor under(ctx);
        executor_ref ex(under);

        BOOST_TEST_EQ(ctx.work, 0);
        ex.on_work_started();
        BOOST_TEST_EQ(ctx.work, 1);
        ex.on_work_finished();
        BOOST_TEST_EQ(ctx.work, 0);
    }

    void
    testDispatch()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();
        executor_ref ex(executor);

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
        executor_ref ex(executor);

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
        executor_ref ex(executor);

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
    testTarget()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();
        executor_ref ex(executor);

        // Matching type returns non-null
        auto* p = ex.target<thread_pool::executor_type>();
        BOOST_TEST_NE(p, nullptr);

        // Const overload
        executor_ref const& cex = ex;
        auto* cp = cex.target<thread_pool::executor_type>();
        BOOST_TEST_NE(cp, nullptr);

        // Wrong type returns nullptr
        test::blocking_context bctx;
        auto ie = bctx.get_executor();
        executor_ref ex2(ie);
        BOOST_TEST_EQ(
            ex2.target<thread_pool::executor_type>(),
            nullptr);

        // Wrong type through the const overload returns nullptr.
        executor_ref const& cex2 = ex2;
        BOOST_TEST_EQ(
            cex2.target<thread_pool::executor_type>(),
            nullptr);
    }

    void
    run()
    {
        testConstruct();
        testCopy();
        testEquality();
        testWorkTracking();
        testDispatch();
        testPost();
        testMultiplePost();
        testTarget();
    }
};

TEST_SUITE(
    executor_ref_test,
    "boost.capy.executor_ref");

} // capy
} // boost
