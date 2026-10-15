//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/abandon.hpp>

#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/io_awaitable_promise_base.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <new>
#include <queue>
#include <semaphore>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "test_helpers.hpp"

namespace boost {
namespace capy {

namespace {

// Parks the awaiting coroutine without the environment protocol,
// taking the owner from its promise instead.
struct plain_park
{
    parked* slot;

    bool await_ready() const noexcept { return false; }

    template<class Promise>
    void await_suspend(std::coroutine_handle<Promise> h) noexcept
    {
        slot->h = h;
        slot->owner = h.promise().environment()->owner;
    }

    void await_resume() const noexcept {}
};

/** A coroutine type whose promise deallocates its frame itself.

    Its allocation goes through the frame allocator in scope, like
    `frame_alloc_mixin`, but its `operator delete` calls nothing else.
*/
struct own_delete_task
{
    struct promise_type
        : io_awaitable_promise_base<promise_type>
    {
        static void* operator new(std::size_t size)
        {
            auto* mr = get_current_frame_allocator();
            if(!mr)
                mr = std::pmr::get_default_resource();
            auto total = size + sizeof(std::pmr::memory_resource*);
            void* raw = mr->allocate(total, alignof(std::max_align_t));
            std::memcpy(
                static_cast<char*>(raw) + size, &mr,
                sizeof(std::pmr::memory_resource*));
            return raw;
        }

        static void operator delete(void* p, std::size_t size) noexcept
        {
            std::pmr::memory_resource* mr;
            std::memcpy(
                &mr, static_cast<char*>(p) + size,
                sizeof(std::pmr::memory_resource*));
            mr->deallocate(
                p, size + sizeof(std::pmr::memory_resource*),
                alignof(std::max_align_t));
        }

        own_delete_task get_return_object()
        {
            return own_delete_task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() const noexcept { return false; }
                std::coroutine_handle<> await_suspend(
                    std::coroutine_handle<>) const noexcept
                {
                    return p_->continuation();
                }
                void await_resume() const noexcept {}
            };
            return awaiter{this};
        }

        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h_;

    explicit own_delete_task(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }

    own_delete_task(own_delete_task&& o) noexcept
        : h_(std::exchange(o.h_, nullptr))
    {
    }

    ~own_delete_task()
    {
        if(h_)
        {
            auto const* env = h_.promise().environment();
            auto* owner = env ? env->owner : nullptr;
            if(lent_ && !h_.done() && !(owner && owner->abandoned()))
                std::terminate();
            h_.destroy();
        }
    }

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> cont, io_env const* env)
    {
        h_.promise().set_continuation(cont);
        h_.promise().set_environment(env);
        lent_ = true;
        return h_;
    }

    void await_resume() noexcept
    {
        lent_ = false;
    }

private:
    bool lent_ = false;
};

// Destroys a parked coroutine of another chain when the frame that
// owns it as a by-value parameter is destroyed.
struct holder_param
{
    parked* h;

    explicit holder_param(parked& slot) noexcept
        : h(&slot)
    {
    }

    holder_param(holder_param&& o) noexcept
        : h(std::exchange(o.h, nullptr))
    {
    }

    ~holder_param()
    {
        if(h && *h)
        {
            auto c = std::exchange(*h, parked{});
            capy::abandon(c);
        }
    }
};

/** Parks any number of coroutines; destroying it destroys them all.

    Plays a holder whose teardown runs inside an outer one.
*/
struct parking_lot
{
    std::vector<continuation> hs;

    ~parking_lot()
    {
        for(auto& c : hs)
            capy::abandon(c);
    }

    struct awaiter
    {
        parking_lot* lot;

        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            lot->hs.push_back(continuation{h, nullptr, env->owner});
            return std::noop_coroutine();
        }

        void await_resume() const noexcept {}
    };

    awaiter park() noexcept { return awaiter{this}; }
};

// Records the thread that destroys the frame holding it.
struct thread_mark
{
    std::thread::id* where;

    explicit thread_mark(std::thread::id& w) noexcept
        : where(&w)
    {
    }

    thread_mark(thread_mark&& o) noexcept
        : where(std::exchange(o.where, nullptr))
    {
    }

    ~thread_mark()
    {
        if(where)
            *where = std::this_thread::get_id();
    }
};

// Parks the awaiting coroutine and signals that it did.
struct signal_park
{
    parked* slot;
    std::binary_semaphore* done;

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, io_env const* env) noexcept
    {
        slot->h = h;
        slot->owner = env->owner;
        done->release();
        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

// Counts posts and runs nothing.
struct counting_executor
{
    int* posts;

    bool operator==(counting_executor const& o) const noexcept
    {
        return posts == o.posts;
    }
    test_io_context& context() const noexcept
    {
        return default_test_io_context();
    }
    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}
    std::coroutine_handle<> dispatch(continuation& c) const
    {
        post(c);
        return std::noop_coroutine();
    }
    void post(continuation&) const
    {
        ++*posts;
    }
};

} // namespace

struct abandon_test
{
    // A holder owned by a frame that the teardown destroys: every
    // parked coroutine's parent is collected, with no limit on how many.
    void testCascadeDestroysThousandWaiters()
    {
        constexpr int count = 1000;
        int inner_down = 0, outer_down = 0, server_down = 0;
        parked slot;
        std::queue<continuation> q;
        queuing_executor ex(q);
        auto inner = [](parking_lot& l, destroy_count_t) -> task<void>
        {
            co_await l.park();
        };
        auto outer = [&](parking_lot& l, destroy_count_t) -> task<void>
        {
            co_await inner(l, destroy_count_t(inner_down));
        };
        auto leaf = [](parked* s) -> task<void>
        {
            co_await park_awaitable{s};
        };
        auto server = [&](destroy_count_t) -> task<void>
        {
            parking_lot lot;
            for(int i = 0; i < count; ++i)
                run_async(ex)(outer(lot, destroy_count_t(outer_down)));
            co_await leaf(&slot);
        };
        run_async(ex)(server(destroy_count_t(server_down)));
        pump(q);
        BOOST_TEST(slot);
        BOOST_TEST_EQ(inner_down, 0);

        capy::abandon(slot);
        pump(q);
        BOOST_TEST_EQ(inner_down, count);
        BOOST_TEST_EQ(outer_down, count);
        BOOST_TEST_EQ(server_down, 1);
    }

    // A teardown started from a thread_local destructor, as its thread
    // exits, destroys the chain and frees every frame.
    void testDestroyFromThreadLocalDestructor()
    {
        alloc_counts st;
        int child_down = 0, parent_down = 0;
        parked slot;
        {
            auto child = [](parked* s, destroy_count_t)
                -> task<void>
            {
                co_await park_awaitable{s};
            };
            auto parent = [&](destroy_count_t) -> task<void>
            {
                co_await child(&slot, destroy_count_t(child_down));
            };
            int n = 0;
            test_executor ex(n);
            run_async(ex, counting_allocator<std::byte>(st))(
                parent(destroy_count_t(parent_down)));
            BOOST_TEST(slot);
            std::thread([&slot] {
                struct exit_holder
                {
                    parked* h;
                    ~exit_holder() { capy::abandon(*h); }
                };
                thread_local exit_holder holder{&slot};
            }).join();
        }
        BOOST_TEST_EQ(child_down, 1);
        BOOST_TEST_EQ(parent_down, 1);
        BOOST_TEST(st.balanced());
    }

    // A thread_local holder made before its thread first allocates a
    // frame outlives that thread's frame cache; the frames its teardown
    // frees at exit are released, not cached where nothing frees them.
    // A leak here shows only under LeakSanitizer.
    void testDestroyAtThreadExitAfterCacheDrained()
    {
        int child_down = 0, parent_down = 0;
        std::thread([&] {
            struct exit_holder
            {
                parked h;
                ~exit_holder()
                {
                    if(h)
                        capy::abandon(h);
                }
            };
            thread_local exit_holder holder;
            auto child = [](parked* s, destroy_count_t)
                -> task<void>
            {
                co_await park_awaitable{s};
            };
            auto parent = [&](destroy_count_t) -> task<void>
            {
                co_await child(&holder.h, destroy_count_t(child_down));
            };
            int n = 0;
            test_executor ex(n);
            run_async(ex)(parent(destroy_count_t(parent_down)));
            BOOST_TEST(holder.h);
        }).join();
        BOOST_TEST_EQ(child_down, 1);
        BOOST_TEST_EQ(parent_down, 1);
    }

    // A teardown started while an exception unwinds the stack destroys
    // the chain as usual.
    void testDestroyDuringUnwinding()
    {
        int child_down = 0, parent_down = 0;
        parked slot;
        auto child = [](parked* s, destroy_count_t)
            -> task<void>
        {
            co_await park_awaitable{s};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            co_await child(&slot, destroy_count_t(child_down));
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex)(parent(destroy_count_t(parent_down)));
        BOOST_TEST(slot);

        struct unwind_holder
        {
            parked* h;
            ~unwind_holder() { capy::abandon(*h); }
        };
        try
        {
            unwind_holder holder{&slot};
            throw std::runtime_error("unwind");
        }
        catch(std::runtime_error const&)
        {
        }
        BOOST_TEST_EQ(child_down, 1);
        BOOST_TEST_EQ(parent_down, 1);
    }

    // A promise that frees its own frame through the allocator held by
    // the run_async trampoline needs nothing from capy: the trampoline
    // is destroyed only after that frame is freed.
    void testOwnOperatorDeleteNeedsNoHook()
    {
        alloc_counts st;
        int child_down = 0, parent_down = 0;
        parked slot;
        {
            auto child = [](parked* s, destroy_count_t) -> own_delete_task
            {
                co_await plain_park{s};
            };
            auto parent = [&](parked* s, destroy_count_t)
                -> task<void>
            {
                co_await child(s, destroy_count_t(child_down));
            };
            int n = 0;
            test_executor ex(n);
            run_async(ex, counting_allocator<std::byte>(st))(
                parent(&slot, destroy_count_t(parent_down)));
            BOOST_TEST(slot);

            capy::abandon(slot);
        }
        BOOST_TEST_EQ(child_down, 1);
        BOOST_TEST_EQ(parent_down, 1);
        BOOST_TEST(st.allocs > 0);
        BOOST_TEST(st.balanced());
    }

    // A teardown started while a frame is between its promise and its
    // deallocation leaves that frame's parent alone: the parent's chain
    // owns the allocator the frame is about to be freed through.
    void testNestedDestroyKeepsOuterParent()
    {
        alloc_counts st;
        int parent_down = 0, other_down = 0, other_parent_down = 0;
        parked slot, other;
        auto other_leaf = [](parked* s, destroy_count_t)
            -> task<void>
        {
            co_await park_awaitable{s};
        };
        auto other_parent = [&](destroy_count_t) -> task<void>
        {
            co_await other_leaf(&other, destroy_count_t(other_down));
        };
        auto leaf = [](holder_param, parked* s)
            -> task<void>
        {
            co_await park_awaitable{s};
        };
        auto parent = [&](destroy_count_t) -> task<void>
        {
            co_await leaf(holder_param(other), &slot);
        };
        int n = 0;
        test_executor ex(n);
        run_async(ex)(other_parent(destroy_count_t(other_parent_down)));
        run_async(ex, counting_allocator<std::byte>(st))(
            parent(destroy_count_t(parent_down)));
        BOOST_TEST(slot);
        BOOST_TEST(other);

        capy::abandon(slot);
        BOOST_TEST_EQ(other_down, 1);
        BOOST_TEST_EQ(other_parent_down, 1);
        BOOST_TEST_EQ(parent_down, 1);
        BOOST_TEST(st.allocs > 0);
        BOOST_TEST(st.balanced());
    }

    // A chain is unwound on its owner's executor, not on the thread of
    // the holder that gives it up.
    void testUnwindRunsOnOwnersExecutor()
    {
        std::thread::id child_at, root_at;
        std::thread::id pool_thread;
        parked slot;
        std::binary_semaphore done{0};
        thread_pool pool(1);
        auto child = [&](thread_mark) -> task<void>
        {
            co_await signal_park{&slot, &done};
        };
        auto root = [&](thread_mark) -> task<void>
        {
            pool_thread = std::this_thread::get_id();
            co_await child(thread_mark(child_at));
        };
        run_async(pool.get_executor())(root(thread_mark(root_at)));
        // Bounds a hang, not a timing assertion.
        if(!BOOST_TEST(done.try_acquire_for(std::chrono::seconds(30))))
            return;

        capy::abandon(slot);
        pool.join();
        BOOST_TEST(child_at == pool_thread);
        BOOST_TEST(root_at == pool_thread);
        BOOST_TEST(pool_thread != std::this_thread::get_id());
    }

    // Across run on another executor, the inner segment is unwound on
    // that executor and the parent then on the caller's.
    void testUnwindAcrossRunBoundary()
    {
        std::thread::id child_at, parent_at;
        std::thread::id inner_thread, caller_thread;
        parked slot;
        std::binary_semaphore done{0};
        thread_pool caller(1);
        thread_pool inner(1);
        auto child = [&](thread_mark) -> task<void>
        {
            inner_thread = std::this_thread::get_id();
            co_await signal_park{&slot, &done};
        };
        auto parent = [&](thread_mark) -> task<void>
        {
            caller_thread = std::this_thread::get_id();
            co_await capy::run(inner.get_executor())(
                child(thread_mark(child_at)));
        };
        run_async(caller.get_executor())(parent(thread_mark(parent_at)));
        // Bounds a hang, not a timing assertion.
        if(!BOOST_TEST(done.try_acquire_for(std::chrono::seconds(30))))
            return;

        capy::abandon(slot);
        // The launch holds work on the caller until its unwind is done,
        // which follows the inner one.
        caller.join();
        inner.join();
        BOOST_TEST(child_at == inner_thread);
        BOOST_TEST(parent_at == caller_thread);
        BOOST_TEST(inner_thread != caller_thread);
    }

    // A segment is unwound once however many times it is abandoned.
    void testSecondAbandonDoesNothing()
    {
        int posts = 0;
        counting_executor ex{&posts};
        chain_owner owner;
        owner.bind(std::noop_coroutine(), ex);
        owner.abandon();
        owner.abandon();
        BOOST_TEST(owner.abandoned());
        BOOST_TEST_EQ(posts, 1);
    }

    // A continuation with no owner is destroyed directly.
    void testAbandonWithoutOwnerDestroys()
    {
        int down = 0;
        auto probe = [](destroy_count_t) -> task<void> { co_return; };
        auto t = probe(destroy_count_t(down));
        continuation c{t.handle()};
        t.release();
        capy::abandon(c);
        BOOST_TEST_EQ(down, 1);
    }

    void
    run()
    {
        testCascadeDestroysThousandWaiters();
        testDestroyFromThreadLocalDestructor();
        testDestroyDuringUnwinding();
        testDestroyAtThreadExitAfterCacheDrained();
        testOwnOperatorDeleteNeedsNoHook();
        testNestedDestroyKeepsOuterParent();
        testUnwindRunsOnOwnersExecutor();
        testUnwindAcrossRunBoundary();
        testSecondAbandonDoesNothing();
        testAbandonWithoutOwnerDestroys();
    }
};

TEST_SUITE(
    abandon_test,
    "boost.capy.ex.abandon");

} // capy
} // boost
