//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/9.design/9k.Executor.adoc.

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

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
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

using namespace boost::capy;

// The page shows the concept exactly as defined in
// <boost/capy/concept/executor.hpp>; compiling a copy keeps the page
// in sync with the real definition.
namespace concept_def {

// tag::executor_concept[]
template<class E>
concept Executor =
    std::is_nothrow_copy_constructible_v<E> &&
    std::is_nothrow_move_constructible_v<E> &&
    requires(E& e, E const& ce, E const& ce2,
             continuation c)
    {
        { ce == ce2 } noexcept -> std::convertible_to<bool>;
        { ce.context() } noexcept;
        requires std::is_lvalue_reference_v<
            decltype(ce.context())> &&
            std::derived_from<
                std::remove_reference_t<
                    decltype(ce.context())>,
                execution_context>;
        { ce.on_work_started() } noexcept;
        { ce.on_work_finished() } noexcept;

        { ce.dispatch(c) } -> std::same_as<std::coroutine_handle<>>;
        { ce.post(c) };
    };
// end::executor_concept[]

} // namespace concept_def

static_assert(concept_def::Executor<thread_pool::executor_type>);
static_assert(concept_def::Executor<executor_ref>);
static_assert(!concept_def::Executor<int>);

// Scaffolding context so the conforming dispatch shown on the page
// compiles.
class dispatching_executor
{
    struct
    {
        bool running_in_this_thread() const noexcept { return true; }
    } ctx_;

    void post(continuation&) const {}

public:
    // tag::dispatch_impl[]
    std::coroutine_handle<> dispatch(continuation& c) const
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
    explicit tcp_socket(execution_context&) {}

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
    void const* ex_;                       // pointer to the executor
    detail::executor_vtable const* vt_;    // pointer to the vtable
};
// end::executor_ref_layout[]

} // namespace layout_sketch

static_assert(sizeof(layout_sketch::executor_ref)
    == sizeof(capy::executor_ref));

// I/O awaitable shape: embeds a continuation for the caller's handle
// and an executor_ref for completion dispatch.
struct io_awaitable_sketch
{
    continuation cont_;
    executor_ref ex_;

    // tag::capture_at_initiation[]
    std::coroutine_handle<>
    await_suspend(
        std::coroutine_handle<> h,
        io_env const* env) noexcept
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

namespace safe_resume_def {

// tag::safe_resume[]
inline void
safe_resume(std::coroutine_handle<> h) noexcept
{
    auto* saved = get_current_frame_allocator();
    h.resume();
    set_current_frame_allocator(saved);
}
// end::safe_resume[]

} // namespace safe_resume_def

// Declarations are enough for the concept check; the definitions are
// a real implementation's concern.
// tag::minimal_executor[]
class my_executor
{
public:
    execution_context& context() const noexcept;

    void on_work_started() const noexcept;
    void on_work_finished() const noexcept;

    std::coroutine_handle<> dispatch(continuation& c) const;
    void post(continuation& c) const;

    bool operator==(my_executor const&) const noexcept;
};
// end::minimal_executor[]

static_assert(capy::Executor<my_executor>);

struct executor_design_test
{
    void testDispatchReturnsHandle()
    {
        dispatching_executor ex;
        continuation c;
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
        thread_pool pool(1);
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
