//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/executor.hpp>

#include <boost/capy/task.hpp>

#include "test_suite.hpp"

#include <boost/system/result.hpp>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <vector>

namespace boost {
namespace capy {

//-----------------------------------------------------------------------------

/** Simple synchronous executor for testing.

    Executes work immediately in the calling thread.
    Uses malloc/free for allocation with a header to track size.
*/
struct sync_executor
{
    friend struct executor::access;

    std::atomic<int> alloc_count{0};
    std::atomic<int> submit_count{0};

private:
    struct header
    {
        std::size_t size;
    };

    void*
    allocate(std::size_t size, std::size_t /*align*/)
    {
        ++alloc_count;
        std::size_t total = sizeof(header) + size;
        void* p = std::malloc(total);
        auto* h = new(p) header{total};
        return h + 1;
    }

    void
    deallocate(void* p, std::size_t /*size*/, std::size_t /*align*/)
    {
        auto* h = static_cast<header*>(p) - 1;
        std::free(h);
    }

    void
    submit(executor::work* w)
    {
        ++submit_count;
        w->invoke();
        w->~work();
        deallocate(w, 0, 0);
    }
};

/** Entry for queued work.
*/
struct queued_entry
{
    executor::work* w;
    void* storage;
};

/** Value-type executor for testing owning mode.

    This executor can be moved and copied, suitable for
    use with executor::from().
*/
struct value_executor
{
    friend struct executor::access;

    // Shared state to track calls across copies
    struct state
    {
        std::atomic<int> alloc_count{0};
        std::atomic<int> submit_count{0};
    };

    std::shared_ptr<state> state_;

    value_executor()
        : state_(std::make_shared<state>())
    {
    }

    // Copyable and movable
    value_executor(value_executor const&) = default;
    value_executor(value_executor&&) = default;
    value_executor& operator=(value_executor const&) = default;
    value_executor& operator=(value_executor&&) = default;

    int alloc_count() const { return state_->alloc_count.load(); }
    int submit_count() const { return state_->submit_count.load(); }

private:
    struct header
    {
        std::size_t size;
    };

    void*
    allocate(std::size_t size, std::size_t /*align*/)
    {
        ++state_->alloc_count;
        std::size_t total = sizeof(header) + size;
        void* p = std::malloc(total);
        header* h = new(p) header{total};
        return h + 1;
    }

    void
    deallocate(void* p, std::size_t /*size*/, std::size_t /*align*/)
    {
        header* h = static_cast<header*>(p) - 1;
        std::free(h);
    }

    void
    submit(executor::work* w)
    {
        ++state_->submit_count;
        w->invoke();
        w->~work();
        deallocate(w, 0, 0);
    }
};

//-----------------------------------------------------------------------------

struct execution_test
{
    void
    testDefaultConstruct()
    {
        executor exec;
        BOOST_TEST(!exec);
    }

    void
    testConstructFromImpl()
    {
        sync_executor ctx;
        executor exec(ctx);
        BOOST_TEST(static_cast<bool>(exec));
    }

    void
    testCopyConstruct()
    {
        sync_executor ctx;
        executor exec1(ctx);
        executor exec2(exec1);
        BOOST_TEST(static_cast<bool>(exec1));
        BOOST_TEST(static_cast<bool>(exec2));
    }

    void
    testMoveConstruct()
    {
        sync_executor ctx;
        executor exec1(ctx);
        executor exec2(std::move(exec1));
        BOOST_TEST(static_cast<bool>(exec2));
    }

    void
    testCopyAssign()
    {
        sync_executor ctx;
        executor exec1(ctx);
        executor exec2;
        exec2 = exec1;
        BOOST_TEST(static_cast<bool>(exec1));
        BOOST_TEST(static_cast<bool>(exec2));
    }

    void
    testMoveAssign()
    {
        sync_executor ctx;
        executor exec1(ctx);
        executor exec2;
        exec2 = std::move(exec1);
        BOOST_TEST(static_cast<bool>(exec2));
    }

    void
    testPostLambda()
    {
        bool called = false;
        sync_executor ctx;
        executor exec(ctx);
        exec.post([&called]{ called = true; });
        BOOST_TEST(called);
    }

    void
    testPostMultiple()
    {
        int count = 0;
        sync_executor ctx;
        executor exec(ctx);
        exec.post([&count]{ ++count; });
        exec.post([&count]{ ++count; });
        exec.post([&count]{ ++count; });
        BOOST_TEST_EQ(count, 3);
    }

    void
    testPostWithCapture()
    {
        int result = 0;
        int a = 10, b = 20;
        sync_executor ctx;
        executor exec(ctx);
        exec.post([&result, a, b]{ result = a + b; });
        BOOST_TEST_EQ(result, 30);
    }

    void
    testPostWithMoveOnlyCapture()
    {
        struct callable
        {
            int& result;
            std::unique_ptr<int> ptr;

            void operator()()
            {
                result = *ptr;
            }
        };

        int result = 0;
        sync_executor ctx;
        executor exec(ctx);
        std::unique_ptr<int> ptr(new int(42));
        exec.post(callable{result, std::move(ptr)});
        BOOST_TEST_EQ(result, 42);
    }

    void
    testQueuedExecution()
    {
        // Queued executor using shared state via pointers
        struct shared_queue_executor
        {
            friend struct executor::access;

            std::vector<queued_entry>* queue;

        private:
            void* allocate(std::size_t size, std::size_t)
            {
                return std::malloc(size);
            }

            void deallocate(void* p, std::size_t, std::size_t)
            {
                std::free(p);
            }

            void submit(executor::work* w)
            {
                queue->push_back({w, w});
            }
        };

        int count = 0;
        std::vector<queued_entry> queue;
        shared_queue_executor ctx{&queue};
        executor exec(ctx);

        exec.post([&count]{ ++count; });
        exec.post([&count]{ ++count; });

        BOOST_TEST_EQ(count, 0);
        BOOST_TEST_EQ(queue.size(), 2u);

        // Run one
        if(!queue.empty())
        {
            auto e = queue.front();
            queue.erase(queue.begin());
            e.w->invoke();
            e.w->~work();
            std::free(e.storage);
        }
        BOOST_TEST_EQ(count, 1);

        // Run remaining
        while(!queue.empty())
        {
            auto e = queue.front();
            queue.erase(queue.begin());
            e.w->invoke();
            e.w->~work();
            std::free(e.storage);
        }
        BOOST_TEST_EQ(count, 2);
    }

    void
    testSharedReference()
    {
        int count = 0;
        sync_executor ctx;
        executor exec1(ctx);
        executor exec2 = exec1;

        exec1.post([&count]{ ++count; });
        exec2.post([&count]{ ++count; });

        BOOST_TEST_EQ(count, 2);
        // Both executors reference the same context
        BOOST_TEST_EQ(ctx.submit_count.load(), 2);
    }

    void
    testAsyncPostNonVoid()
    {
        int result = 0;
        bool handler_called = false;

        sync_executor ctx;
        executor exec(ctx);
        exec.async_post(
            []{ return 42; },
            [&](system::result<int, std::exception_ptr> r)
            {
                handler_called = true;
                if(r.has_value())
                    result = r.value();
            });

        BOOST_TEST(handler_called);
        BOOST_TEST_EQ(result, 42);
    }

    void
    testAsyncPostVoid()
    {
        bool work_called = false;
        bool handler_called = false;

        sync_executor ctx;
        executor exec(ctx);
        exec.async_post(
            [&work_called]{ work_called = true; },
            [&handler_called](system::result<void, std::exception_ptr>)
            {
                handler_called = true;
            });

        BOOST_TEST(work_called);
        BOOST_TEST(handler_called);
    }

    void
    testAsyncPostException()
    {
        bool handler_called = false;
        bool got_exception = false;

        sync_executor ctx;
        executor exec(ctx);
        exec.async_post(
            []() -> int { throw std::runtime_error("test"); },
            [&](system::result<int, std::exception_ptr> r)
            {
                handler_called = true;
                if(r.has_error())
                    got_exception = true;
            });

        BOOST_TEST(handler_called);
        BOOST_TEST(got_exception);
    }

    void
    testFactoryBasic()
    {
        sync_executor ctx;
        executor exec(ctx);

        struct my_work : executor::work
        {
            bool& flag;
            explicit my_work(bool& f) : flag(f) {}
            void invoke() override { flag = true; }
        };

        bool invoked = false;
        {
            executor::factory fac(exec);
            void* p = fac.allocate(sizeof(my_work), alignof(my_work));
            auto* w = ::new(p) my_work(invoked);
            fac.commit(w);
        }
        BOOST_TEST(invoked);
    }

    void
    testFactoryRollback()
    {
        // Use a tracking structure
        struct tracking_executor
        {
            friend struct executor::access;

            int alloc_count = 0;
            int dealloc_count = 0;

        private:
            struct header { std::size_t size; };

            void* allocate(std::size_t size, std::size_t)
            {
                ++alloc_count;
                std::size_t total = sizeof(header) + size;
                void* p = std::malloc(total);
                header* h = new(p) header{total};
                return h + 1;
            }

            void deallocate(void* p, std::size_t, std::size_t)
            {
                ++dealloc_count;
                header* h = static_cast<header*>(p) - 1;
                std::free(h);
            }

            void submit(executor::work* w)
            {
                w->invoke();
                w->~work();
                deallocate(w, 0, 0);
            }
        };

        tracking_executor ctx;
        executor exec(ctx);

        {
            executor::factory fac(exec);
            fac.allocate(64, 8);
            // No commit - destructor should deallocate
        }

        BOOST_TEST_EQ(ctx.alloc_count, 1);
        BOOST_TEST_EQ(ctx.dealloc_count, 1);
    }

    //-------------------------------------------------------------------------
    // Owning mode tests (executor::wrap)
    //-------------------------------------------------------------------------

    void
    testWrapBasic()
    {
        value_executor ve;
        executor exec = executor::wrap(ve);
        BOOST_TEST(static_cast<bool>(exec));
    }

    void
    testWrapTemporary()
    {
        executor exec = executor::wrap(value_executor{});
        BOOST_TEST(static_cast<bool>(exec));
    }

    void
    testWrapCopyConstruct()
    {
        value_executor ve;
        executor exec1 = executor::wrap(ve);
        executor exec2(exec1);
        BOOST_TEST(static_cast<bool>(exec1));
        BOOST_TEST(static_cast<bool>(exec2));
    }

    void
    testWrapMoveConstruct()
    {
        value_executor ve;
        executor exec1 = executor::wrap(ve);
        executor exec2(std::move(exec1));
        BOOST_TEST(static_cast<bool>(exec2));
    }

    void
    testWrapCopyAssign()
    {
        value_executor ve;
        executor exec1 = executor::wrap(ve);
        executor exec2;
        exec2 = exec1;
        BOOST_TEST(static_cast<bool>(exec1));
        BOOST_TEST(static_cast<bool>(exec2));
    }

    void
    testWrapMoveAssign()
    {
        value_executor ve;
        executor exec1 = executor::wrap(ve);
        executor exec2;
        exec2 = std::move(exec1);
        BOOST_TEST(static_cast<bool>(exec2));
    }

    void
    testWrapPostLambda()
    {
        bool called = false;
        value_executor ve;
        executor exec = executor::wrap(ve);
        exec.post([&called]{ called = true; });
        BOOST_TEST(called);
        BOOST_TEST_EQ(ve.submit_count(), 1);
    }

    void
    testWrapPostMultiple()
    {
        int count = 0;
        value_executor ve;
        executor exec = executor::wrap(ve);
        exec.post([&count]{ ++count; });
        exec.post([&count]{ ++count; });
        exec.post([&count]{ ++count; });
        BOOST_TEST_EQ(count, 3);
        BOOST_TEST_EQ(ve.submit_count(), 3);
    }

    void
    testWrapSharedOwnership()
    {
        // Verify that copies share the same underlying executor
        int count = 0;
        value_executor ve;
        executor exec1 = executor::wrap(ve);
        executor exec2 = exec1;

        exec1.post([&count]{ ++count; });
        exec2.post([&count]{ ++count; });

        BOOST_TEST_EQ(count, 2);
        // Both executors should use the same underlying value_executor
        BOOST_TEST_EQ(ve.submit_count(), 2);
    }

    void
    testWrapLifetime()
    {
        // Verify executor remains valid after original goes out of scope
        executor exec;
        {
            value_executor ve;
            exec = executor::wrap(ve);
        }
        // ve is destroyed but exec should still work
        // (the holder keeps a copy of the value_executor)
        BOOST_TEST(static_cast<bool>(exec));

        bool called = false;
        exec.post([&called]{ called = true; });
        BOOST_TEST(called);
    }

    void
    testWrapAsyncPost()
    {
        int result = 0;
        bool handler_called = false;

        value_executor ve;
        executor exec = executor::wrap(ve);
        exec.async_post(
            []{ return 42; },
            [&](system::result<int, std::exception_ptr> r)
            {
                handler_called = true;
                if(r.has_value())
                    result = r.value();
            });

        BOOST_TEST(handler_called);
        BOOST_TEST_EQ(result, 42);
    }

    void
    run()
    {
        testDefaultConstruct();
        testConstructFromImpl();
        testCopyConstruct();
        testMoveConstruct();
        testCopyAssign();
        testMoveAssign();
        testPostLambda();
        testPostMultiple();
        testPostWithCapture();
        testPostWithMoveOnlyCapture();
        testQueuedExecution();
        testSharedReference();
        testAsyncPostNonVoid();
        testAsyncPostVoid();
        testAsyncPostException();
        testFactoryBasic();
        testFactoryRollback();

        // Owning mode tests
        testWrapBasic();
        testWrapTemporary();
        testWrapCopyConstruct();
        testWrapMoveConstruct();
        testWrapCopyAssign();
        testWrapMoveAssign();
        testWrapPostLambda();
        testWrapPostMultiple();
        testWrapSharedOwnership();
        testWrapLifetime();
        testWrapAsyncPost();
    }
};

TEST_SUITE(
    execution_test,
    "boost.capy.execution");

//-----------------------------------------------------------------------------

#ifdef BOOST_CAPY_HAS_CORO

template<class T>
T run_task_exec(task<T>& t)
{
    while(!t.handle().done())
        t.handle().resume();
    return t.await_resume();
}

inline void run_task_exec(task<void>& t)
{
    while(!t.handle().done())
        t.handle().resume();
    t.await_resume();
}

struct execution_coro_test
{
    void
    testAsyncPostAwaitableNonVoid()
    {
        sync_executor ctx;

        auto run = [&ctx]() -> task<int>
        {
            executor exec(ctx);
            int result = co_await exec.async_post([]{ return 42; });
            co_return result;
        };

        auto t = run();
        BOOST_TEST_EQ(run_task_exec(t), 42);
    }

    void
    testAsyncPostAwaitableVoid()
    {
        bool executed = false;
        sync_executor ctx;

        auto run = [&ctx, &executed]() -> task<void>
        {
            executor exec(ctx);
            co_await exec.async_post([&executed]{ executed = true; });
            co_return;
        };

        auto t = run();
        run_task_exec(t);
        BOOST_TEST(executed);
    }

    void
    testAsyncPostAwaitableMultiple()
    {
        sync_executor ctx;

        auto run = [&ctx]() -> task<int>
        {
            executor exec(ctx);
            int a = co_await exec.async_post([]{ return 10; });
            int b = co_await exec.async_post([]{ return 20; });
            int c = co_await exec.async_post([]{ return 30; });
            co_return a + b + c;
        };

        auto t = run();
        BOOST_TEST_EQ(run_task_exec(t), 60);
    }

    void
    run()
    {
#if 0
        testAsyncPostAwaitableNonVoid();
        testAsyncPostAwaitableVoid();
        testAsyncPostAwaitableMultiple();
#endif
    }
};

TEST_SUITE(
    execution_coro_test,
    "boost.capy.execution.coro");

#endif

} // capy
} // boost
