//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4g.allocators.adoc.

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

#include <boost/capy/ex/any_executor.hpp>
#include <boost/capy/ex/frame_alloc_mixin.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include <array>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <memory_resource>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

using namespace boost::capy;

std::atomic<int> tasks_completed{0};

task<> my_task()
{
    ++tasks_completed;
    co_return;
}

// Minimal hand-rolled coroutine so a raw handle exists to feed
// safe_resume; task<> never exposes its handle.
struct resumable
{
    struct promise_type
    {
        resumable get_return_object()
        {
            return {std::coroutine_handle<
                promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };

    std::coroutine_handle<> handle;
};

resumable clobber_tls(bool& resumed)
{
    resumed = true;
    // Simulate another chain's await_resume overwriting TLS
    set_current_frame_allocator(nullptr);
    co_return;
}

// tag::frame_alloc_mixin[]
struct my_coroutine
{
    struct promise_type : capy::frame_alloc_mixin
    {
        // get_return_object, initial_suspend, ...
    };
};
// end::frame_alloc_mixin[]

// Shadow copy of the library's task declaration; compiling it here
// proves the shown declaration is the real one.
namespace library_sketch {

// tag::task_elidable[]
template<typename T = void>
struct [[nodiscard]] BOOST_CAPY_CORO_AWAIT_ELIDABLE
    task
{
    // ...
};
// end::task_elidable[]

} // namespace library_sketch

task<int> compute()
{
    co_return 42;
}

task<> halo_demo(std::vector<task<int>>& tasks, int& out)
{
    // tag::halo_patterns[]
    // HALO can apply: task is awaited immediately
    int result = co_await compute();

    // HALO cannot apply: task escapes to storage
    auto t = compute();
    tasks.push_back(std::move(t));
    // end::halo_patterns[]
    out = result;
}

struct item {};

int items_processed = 0;

task<> process(item const&)
{
    ++items_processed;
    co_return;
}

// Bound to a blocking (inline) executor in the test so every frame is
// released before the stack buffer backing the resource goes away.
any_executor executor;

// tag::batch_allocator[]
void process_batch(std::vector<item> const& items)
{
    std::array<std::byte, 64 * 1024> buffer;
    std::pmr::monotonic_buffer_resource resource(
        buffer.data(), buffer.size());

    for (auto const& item : items)
    {
        run_async(executor, &resource)(process(item));
    }
    // All frames deallocated when resource goes out of scope
}
// end::batch_allocator[]

// tag::recycling_observe_resource[]
// A memory resource that pools freed blocks by size. When a coroutine
// frame is freed, its block is kept; the next frame of the same size
// reuses it instead of allocating again -- the strategy that makes
// recycling_memory_resource fast. The two counters let the test observe
// upstream allocations versus reuse.
struct pooling_resource : std::pmr::memory_resource
{
    std::size_t upstream = 0;   // blocks taken from the heap
    std::size_t reused = 0;     // blocks served from the freelist

    void*
    do_allocate(std::size_t bytes, std::size_t) override
    {
        auto& blocks = pool_[bytes];
        if(! blocks.empty())
        {
            ++reused;
            void* p = blocks.back();
            blocks.pop_back();
            return p;
        }
        ++upstream;
        return ::operator new(bytes);
    }

    void
    do_deallocate(void* p, std::size_t bytes, std::size_t) override
    {
        pool_[bytes].push_back(p);   // keep the block for the next frame
    }

    bool
    do_is_equal(memory_resource const& other) const noexcept override
    {
        return this == &other;
    }

    ~pooling_resource() override
    {
        for(auto& [bytes, blocks] : pool_)
            for(void* p : blocks)
                ::operator delete(p);
    }

    std::unordered_map<std::size_t, std::vector<void*>> pool_;
};
// end::recycling_observe_resource[]

struct io_step
{
    std::error_code ec;
    std::size_t n = 0;
};

struct stream
{
    int reads = 0;
    int writes = 0;

    task<io_step> read_some(char*)
    {
        ++reads;
        co_return io_step{};
    }

    task<io_step> write_some(char const*)
    {
        ++writes;
        co_return io_step{};
    }
};

task<> do_work(char*, std::size_t)
{
    co_return;
}

void prepare(char*, char const*, std::size_t) {}

void prepare(char*, std::size_t) {}

namespace scope_bad {

// tag::frame_scope_bad[]
// BAD: buf lives in frame across all subsequent co_awaits
task<> process(stream& s)
{
    char reply[] = "ok";
    char buf[4096];
    auto [ec, n] = co_await s.read_some(buf);
    co_await do_work(buf, n);
    co_await s.write_some(reply);   // buf wastes 4K in frame
}
// end::frame_scope_bad[]

} // namespace scope_bad

namespace scope_good {

// tag::frame_scope_good[]
// GOOD: braces end buf's lifetime before next suspend
task<> process(stream& s)
{
    char reply[] = "ok";
    {
        char buf[4096];
        auto [ec, n] = co_await s.read_some(buf);
        co_await do_work(buf, n);
    }
    co_await s.write_some(reply);  // 4K saved
}
// end::frame_scope_good[]

} // namespace scope_good

namespace overlap_bad {

// tag::pipeline_overlap_bad[]
// BAD: both arrays in frame simultaneously (8K)
task<> pipeline(stream& in, stream& out)
{
    char read_buf[4096];
    auto [ec1, n] = co_await in.read_some(read_buf);

    char write_buf[4096];
    prepare(write_buf, read_buf, n);
    co_await out.write_some(write_buf);
}
// end::pipeline_overlap_bad[]

} // namespace overlap_bad

namespace overlap_good {

// tag::pipeline_overlap_good[]
// GOOD: non-overlapping scopes allow frame reuse (4K)
task<> pipeline(stream& in, stream& out)
{
    std::size_t n;
    {
        char read_buf[4096];
        auto [ec, n_] = co_await in.read_some(read_buf);
        n = n_;
    }
    {
        char write_buf[4096];
        prepare(write_buf, n);
        co_await out.write_some(write_buf);
    }
}
// end::pipeline_overlap_good[]

} // namespace overlap_good

struct allocators_test
{
    void
    testTwoCallSyntax()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();
        int const before = tasks_completed.load();
        // tag::two_call[]
        run_async(executor)(my_task());
        //        ↑         ↑
        //        1. Sets    2. Task allocated
        //        TLS        using TLS allocator
        // end::two_call[]
        pool.join();
        BOOST_TEST(tasks_completed.load() == before + 1);
    }

    void
    testSafeResume()
    {
        // The sentinel proves TLS survives a resume that clobbers it
        std::pmr::monotonic_buffer_resource sentinel;
        set_current_frame_allocator(&sentinel);
        bool resumed = false;
        auto r = clobber_tls(resumed);
        std::coroutine_handle<> h = r.handle;
        // tag::safe_resume[]
        // In your event loop or dispatch path:
        capy::safe_resume(h);   // saves and restores TLS around h.resume()
        // end::safe_resume[]
        BOOST_TEST(resumed);
        BOOST_TEST(get_current_frame_allocator() == &sentinel);
        set_current_frame_allocator(nullptr);
        r.handle.destroy();
    }

    void
    testRunAsyncPmrAllocator()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();
        int const before = tasks_completed.load();
        // tag::run_async_pmr_alloc[]
        std::pmr::monotonic_buffer_resource resource;
        std::pmr::polymorphic_allocator<std::byte> alloc(&resource);

        run_async(executor, alloc)(my_task());
        // end::run_async_pmr_alloc[]
        pool.join();
        BOOST_TEST(tasks_completed.load() == before + 1);
    }

    void
    testRunAsyncMemoryResource()
    {
        thread_pool pool(1);
        auto executor = pool.get_executor();
        int const before = tasks_completed.load();
        // tag::run_async_memory_resource[]
        std::pmr::monotonic_buffer_resource resource;
        run_async(executor, &resource)(my_task());
        // end::run_async_memory_resource[]
        pool.join();
        BOOST_TEST(tasks_completed.load() == before + 1);
    }

    void
    testRecyclingObserved()
    {
        // tag::recycling_observe[]
        // Run the same task repeatedly through one pooling resource. The
        // first run has an empty pool, so its frames come from upstream.
        // Once the task completes, its frames go back into the pool, so
        // every later run reuses a freed block of the right size.
        pooling_resource pooling;

        auto run_once = [&]
        {
            thread_pool pool(1);
            run_async(pool.get_executor(), &pooling)(my_task());
            pool.join();   // task done: its frames are back in the pool
        };

        run_once();                                     // cold: fills pool
        std::size_t const upstream_when_warm = pooling.upstream;

        for(int i = 0; i < 7; ++i)
            run_once();                                 // warm: reuses pool
        // end::recycling_observe[]

        // The frames really were allocated through our resource...
        BOOST_TEST(pooling.upstream > 0);
        // ...and the seven warm runs added no upstream allocations: every
        // frame came from a recycled block.
        BOOST_TEST(pooling.upstream == upstream_when_warm);
        BOOST_TEST(pooling.reused > 0);
    }

    void
    testHaloPatterns()
    {
        std::vector<task<int>> tasks;
        int out = 0;
        test::run_blocking()(halo_demo(tasks, out));
        BOOST_TEST(out == 42);
        BOOST_TEST(tasks.size() == 1u);
        // The escaped task is destroyed unawaited when the vector dies
    }

    void
    testBatchAllocator()
    {
        test::blocking_context ctx;
        executor = ctx.get_executor();
        std::vector<item> items(3);
        int const before = items_processed;
        process_batch(items);
        BOOST_TEST(items_processed == before + 3);
        executor = any_executor();
    }

    void
    testFrameScope()
    {
        stream s;
        test::run_blocking()(scope_bad::process(s));
        test::run_blocking()(scope_good::process(s));
        BOOST_TEST(s.reads == 2);
        BOOST_TEST(s.writes == 2);
    }

    void
    testPipelineOverlap()
    {
        stream in;
        stream out;
        test::run_blocking()(overlap_bad::pipeline(in, out));
        test::run_blocking()(overlap_good::pipeline(in, out));
        BOOST_TEST(in.reads == 2);
        BOOST_TEST(out.writes == 2);
    }

    void
    run()
    {
        testTwoCallSyntax();
        testSafeResume();
        testRunAsyncPmrAllocator();
        testRunAsyncMemoryResource();
        testRecyclingObserved();
        testHaloPatterns();
        testBatchAllocator();
        testFrameScope();
        testPipelineOverlap();
    }
};

} // namespace

TEST_SUITE(allocators_test, "boost.capy.doc.4g_allocators");
