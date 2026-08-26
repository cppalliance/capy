//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/9.design/9k.Executor.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include <atomic>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <type_traits>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {


static_assert(capy::Executor<capy::thread_pool::executor_type>);
static_assert(capy::Executor<capy::executor_ref>);
static_assert(!capy::Executor<int>);

// Scaffolding context so the conforming dispatch shown on the page
// compiles.
class dispatching_executor
{
    struct
    {
        bool running_in_this_thread() const noexcept { return true; }
    } ctx_;

    void post(capy::continuation&) const {}

public:
    // tag::dispatch_impl[]
    std::coroutine_handle<> dispatch(capy::continuation& c) const
    {
        if(ctx_.running_in_this_thread())
            return c.h;            // symmetric transfer
        post(c);
        return std::noop_coroutine();
    }
    // end::dispatch_impl[]
};

namespace continuation_def {

// tag::continuation_struct[]
struct continuation
{
    std::coroutine_handle<> h;
    void* reserved = nullptr;
};
// end::continuation_struct[]

} // namespace continuation_def

static_assert(sizeof(continuation_def::continuation)
    == sizeof(capy::continuation));

// Corosio's contexts implement work tracking over an atomic count;
// this scaffolding reproduces just enough of that shape.
class counting_context
{
    std::atomic<std::size_t> outstanding_work_{1};
    bool stopped_ = false;

    void stop() { stopped_ = true; }

public:
    bool stopped() const noexcept { return stopped_; }

    // tag::on_work_finished[]
    void on_work_finished() noexcept
    {
        if(outstanding_work_.fetch_sub(
            1, std::memory_order_acq_rel) == 1)
            stop();
    }
    // end::on_work_finished[]
};

// Stand-in for Corosio's tcp_socket: the page demonstrates how an I/O
// object accepts any executor and extracts the context via the base
// class reference.
class tcp_socket
{
public:
    explicit tcp_socket(capy::execution_context&) {}

    // tag::socket_ctor[]
    template<class Ex>
        requires capy::Executor<Ex>
    explicit tcp_socket(Ex const& ex)
        : tcp_socket(ex.context())
    {
    }
    // end::socket_ctor[]
};

namespace layout_sketch {

// tag::executor_ref_layout[]
class executor_ref
{
    void const* ex_;                           // pointer to the executor
    capy::detail::executor_vtable const* vt_;  // pointer to the vtable
};
// end::executor_ref_layout[]

} // namespace layout_sketch

static_assert(sizeof(layout_sketch::executor_ref)
    == sizeof(capy::executor_ref));

// I/O awaitable shape: embeds a continuation for the caller's handle
// and an executor_ref for completion dispatch.
struct io_awaitable_sketch
{
    capy::continuation cont_;
    capy::executor_ref ex_;

    // tag::capture_at_initiation[]
    std::coroutine_handle<>
    await_suspend(
        std::coroutine_handle<> h,
        capy::io_env const* env) noexcept
    {
        cont_.h = h;
        ex_ = env->executor;
        // ... initiate I/O operation ...
        return std::noop_coroutine();
    }
    // end::capture_at_initiation[]

    void complete()
    {
        // tag::dispatch_at_completion[]
        // Timer fires or I/O completes:
        ex_.post(cont_);
        // end::dispatch_at_completion[]
    }
};

// Declarations are enough for the concept check; the definitions are
// a real implementation's concern.
// tag::minimal_executor[]
class my_executor
{
public:
    capy::execution_context& context() const noexcept;

    void on_work_started() const noexcept;
    void on_work_finished() const noexcept;

    std::coroutine_handle<> dispatch(capy::continuation& c) const;
    void post(capy::continuation& c) const;

    bool operator==(my_executor const&) const noexcept;
};
// end::minimal_executor[]

static_assert(capy::Executor<my_executor>);

struct executor_design_test
{
    void testDispatchReturnsHandle()
    {
        dispatching_executor ex;
        capy::continuation c;
        BOOST_TEST(ex.dispatch(c) == std::coroutine_handle<>());
    }

    void testWorkCountReachesZero()
    {
        counting_context ctx;
        ctx.on_work_finished();
        BOOST_TEST(ctx.stopped());
    }

    void testSocketFromExecutor()
    {
        capy::thread_pool pool(1);
        tcp_socket sock(pool.get_executor());
        (void)sock;
    }

    void run()
    {
        testDispatchReturnsHandle();
        testWorkCountReachesZero();
        testSocketFromExecutor();
    }
};

} // namespace

TEST_SUITE(executor_design_test, "boost.capy.doc.9k_executor");
