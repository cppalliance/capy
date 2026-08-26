//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/9.design/9l.RunApi.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/strand.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>

#include <atomic>
#include <exception>
#include <iostream>
#include <memory_resource>
#include <stop_token>
#include <utility>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

std::atomic<int> tasks_run{0};
std::atomic<int> connections_handled{0};

capy::task<void> my_task()
{
    ++tasks_run;
    co_return;
}

capy::task<void> cancellable_task() { co_return; }
capy::task<int> compute_value() { co_return 42; }
capy::task<int> compute_on_worker() { co_return 42; }
capy::task<void> subtask() { co_return; }
capy::task<void> cpu_bound_task() { co_return; }

// Executor a coroutine can hop to; kept alive for the whole test run
// because the run_usage fragments name it at namespace scope.
capy::thread_pool worker_pool(1);
capy::thread_pool::executor_type worker_ex = worker_pool.get_executor();
std::pmr::memory_resource* my_alloc = std::pmr::new_delete_resource();

// tag::run_usage[]
// Switch to a different executor for CPU-bound work
capy::task<void> parent()
{
    int result = co_await capy::run(worker_ex)(compute_on_worker());
    // Completion returns to parent's executor
}

// Customize stop token, inherit caller's executor
capy::task<void> with_timeout()
{
    std::stop_source source;
    co_await capy::run(source.get_token())(subtask());
}

// Customize allocator, inherit caller's executor
capy::task<void> with_custom_alloc()
{
    co_await capy::run(my_alloc)(subtask());
}

// Switch executor AND customize allocator
capy::task<void> full_control()
{
    co_await capy::run(worker_ex, my_alloc)(cpu_bound_task());
}
// end::run_usage[]

// Stand-ins for Corosio's I/O types: the page demonstrates launching
// on a strand, not the socket API.
namespace tcp {
struct socket {};
} // namespace tcp

capy::thread_pool& ioc = worker_pool;

capy::task<void> handle_connection(tcp::socket)
{
    ++connections_handled;
    co_return;
}

// tag::run_async_strand[]
void on_accept(tcp::socket sock)
{
    capy::strand my_strand(ioc.get_executor());
    capy::run_async(my_strand)(handle_connection(std::move(sock)));
}
// end::run_async_strand[]

// Mirrors the wrapper in <boost/capy/ex/run_async.hpp>; compiling it
// against the real detail machinery keeps the page in sync.
namespace wrapper_sketch {

template<capy::Executor Ex, class Handlers, class Alloc>
class run_async_wrapper
{
    capy::detail::run_async_trampoline<Ex, Handlers, Alloc> tr_;
    std::stop_token st_;
    std::pmr::memory_resource* saved_tls_;

public:
    // tag::wrapper_tls[]
    run_async_wrapper(Ex ex, std::stop_token st, Handlers h, Alloc a) noexcept
        : tr_(capy::detail::make_trampoline<Ex, Handlers, Alloc>(
            std::move(ex), std::move(h), std::move(a)))
        , st_(std::move(st))
        // remember prior TLS
        , saved_tls_(capy::get_current_frame_allocator())
    {
        // Set TLS before task argument is evaluated
        capy::set_current_frame_allocator(tr_.h_.promise().get_resource());
    }

    ~run_async_wrapper()
    {
        // Restore the prior TLS so a stale pointer does not outlive
        // the execution context that owns the trampoline's resource.
        capy::set_current_frame_allocator(saved_tls_);
    }
    // end::wrapper_tls[]
};

} // namespace wrapper_sketch

// Instantiating every member checks the shown ctor and dtor bodies.
template class wrapper_sketch::run_async_wrapper<
    capy::thread_pool::executor_type,
    capy::detail::default_handler,
    std::pmr::memory_resource*>;

// Simplified: the real mixin in <boost/capy/ex/frame_alloc_mixin.hpp>
// adds a recycling fast path and stores the resource in the frame for
// deallocation.
struct promise_new_sketch
{
    // tag::operator_new[]
    static void* operator new(std::size_t size)
    {
        auto* mr = capy::get_current_frame_allocator();
        if(!mr)
            mr = std::pmr::get_default_resource();
        return mr->allocate(size, alignof(std::max_align_t));
    }
    // end::operator_new[]
};

struct run_api_test
{
    void testRunAsyncForms()
    {
        tasks_run = 0;
        capy::thread_pool pool(2);
        auto ex = pool.get_executor();
        auto* my_pool = std::pmr::new_delete_resource();
        std::stop_token st;
        auto* alloc = std::pmr::new_delete_resource();
        auto h1 = [] {};
        auto h2 = [](std::exception_ptr) {};

        // tag::run_async_usage[]
        // Executor only (uses default recycling allocator)
        capy::run_async(ex)(my_task());

        // With a stop token for cooperative cancellation
        std::stop_source source;
        capy::run_async(ex, source.get_token())(cancellable_task());

        // With a custom memory resource
        capy::run_async(ex, my_pool)(my_task());

        // With a result handler
        capy::run_async(ex, [](int result) {
            std::cout << "Got: " << result << "\n";
        })(compute_value());

        // With separate success and error handlers
        capy::run_async(ex,
            [](int result) { std::cout << "Got: " << result << "\n"; },
            [](std::exception_ptr ep) { /* handle error */ }
        )(compute_value());

        // Full: executor, stop_token, allocator, success handler, error handler
        capy::run_async(ex, st, alloc, h1, h2)(my_task());
        // end::run_async_usage[]

        pool.join();
        BOOST_TEST(tasks_run == 3);
    }

    void testRunForms()
    {
        capy::thread_pool pool(1);
        auto ex = pool.get_executor();
        capy::run_async(ex)(parent());
        capy::run_async(ex)(with_timeout());
        capy::run_async(ex)(with_custom_alloc());
        capy::run_async(ex)(full_control());
        pool.join();
    }

    void testPostfixOrder()
    {
        tasks_run = 0;
        capy::thread_pool pool(1);
        auto ex = pool.get_executor();
        auto* alloc = std::pmr::new_delete_resource();

        // tag::postfix_order[]
        // Step 1: wrapper constructor sets TLS allocator
        //               v~~~~~~~~~~~~~~v
           capy::run_async(ex, alloc)    (my_task());
        //                                ^~~~~~~~~^
        // Step 2: task frame allocated using TLS allocator
        // end::postfix_order[]

        pool.join();
        BOOST_TEST(tasks_run == 1);
    }

    // Must run last: joining the worker pool ends it for good.
    void testStrandAccept()
    {
        on_accept(tcp::socket{});
        worker_pool.join();
        BOOST_TEST(connections_handled == 1);
    }

    void run()
    {
        testRunAsyncForms();
        testRunForms();
        testPostfixOrder();
        testStrandAccept();
    }
};

} // namespace

TEST_SUITE(run_api_test, "boost.capy.doc.9l_run_api");
