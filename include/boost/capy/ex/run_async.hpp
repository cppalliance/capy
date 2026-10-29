//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_RUN_ASYNC_HPP
#define BOOST_CAPY_RUN_ASYNC_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/run.hpp>
#include <boost/capy/detail/run_callbacks.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/context_runnable.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/recycling_memory_resource.hpp>
#include <boost/capy/ex/work_guard.hpp>

#include <coroutine>
#include <cstring>
#include <exception>
#include <memory_resource>
#include <new>
#include <optional>
#include <stop_token>
#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

/** Match types usable as `run_async` completion handlers.

    Excludes the types meaningful to the other `run_async` parameters.
    A stop token, memory resource pointer, or allocator argument
    therefore selects its dedicated overload by conversion. It does not
    deduce as an exact-match handler.
*/
template<class H>
concept RunAsyncHandler =
    !std::is_convertible_v<H, std::pmr::memory_resource*> &&
    !std::is_convertible_v<H, std::stop_token> &&
    !Allocator<H>;

/// Function pointer type for type-erased frame deallocation.
using dealloc_fn = void(*)(void*, std::size_t);

// Report a stopped quitter to the handlers: its stopped value to the
// value handler, or what computing that value threw to the error
// handler. default_handler drops a quitter_stopped. A handler that
// throws is not caught here.
template<class Task, class Handlers>
void invoke_stopped(Handlers& h)
{
    using P = typename Task::promise_type;
    using R = decltype(std::declval<Task&>().await_resume());
    if constexpr(std::is_void_v<R>)
    {
        // A stopped quitter<> has no value: stopped_result only throws.
        // Nothing follows the call, or MSVC warns it is unreachable
        // (C4702).
#if defined(__cpp_exceptions)
        try
        {
            P::stopped_result();
        }
        catch(...)
        {
            h(std::current_exception());
        }
#else
        P::stopped_result();
#endif
    }
    else
    {
        std::optional<R> v;
#if defined(__cpp_exceptions)
        try
        {
            v.emplace(P::stopped_result());
        }
        catch(...)
        {
            h(std::current_exception());
            return;
        }
#else
        v.emplace(P::stopped_result());
#endif
        h(std::move(*v));
    }
}

/// Type-erased deallocator implementation for trampoline frames.
template<class Alloc>
void dealloc_impl(void* raw, std::size_t total)
{
    static_assert(std::is_same_v<typename Alloc::value_type, frame_unit>);
    auto* a = std::launder(reinterpret_cast<Alloc*>(
        static_cast<char*>(raw) + total - sizeof(Alloc)));
    Alloc ua(std::move(*a));
    a->~Alloc();
    ua.deallocate(static_cast<frame_unit*>(raw), frame_units(total));
}

/// Awaiter to access the promise from within the coroutine.
template<class Promise>
struct get_promise_awaiter
{
    Promise* p_ = nullptr;

    bool await_ready() const noexcept { return false; }

    bool await_suspend(std::coroutine_handle<Promise> h) noexcept
    {
        p_ = &h.promise();
        return false;
    }

    Promise& await_resume() const noexcept
    {
        return *p_;
    }
};

// The promise holds Ex, Handlers and the allocator by value. Whether
// an over-aligned one pads the promise depends on the other members'
// sizes, so C4324 is off for both trampolines.
BOOST_CAPY_MSVC_WARNING_PUSH
BOOST_CAPY_MSVC_WARNING_DISABLE(4324) // padded due to alignas

/** Internal run_async_trampoline coroutine for run_async.

    The run_async_trampoline is allocated BEFORE the task (via C++17 postfix evaluation
    order) and serves as the task's continuation. When the task final_suspends,
    control returns to the run_async_trampoline which then invokes the appropriate handler.

    For value-type allocators, the run_async_trampoline stores a frame_memory_resource
    that wraps the allocator. For memory_resource*, it stores the pointer directly.

    @tparam Ex The executor type.
    @tparam Handlers The handler type (default_handler or handler_pair).
    @tparam Alloc The allocator type (value type or memory_resource*).
*/
template<class Ex, class Handlers, class Alloc>
struct run_async_trampoline
{
    using invoke_fn = void(*)(void*, Handlers&, bool);

    struct promise_type
    {
        work_guard<Ex> wg_;
        Handlers handlers_;
        frame_memory_resource<Alloc> resource_;
        await_context env_;
        invoke_fn invoke_ = nullptr;
        void* task_promise_ = nullptr;
        // task_h_: raw handle for frame_guard cleanup in make_trampoline.
        // task_cont_: continuation dispatched to start the task: the same
        // handle, or for a quitter the gate that starts it.
        std::coroutine_handle<> task_h_;
        continuation task_cont_;
        // Owns the task's segment: abandoning it resumes this
        // trampoline, or has it destroyed, to unwind the task.
        chain_owner owner_;
        // The task reports a stop-caused abandon (a quitter). For any
        // other task such an abandon is torn down like any other.
        bool stoppable_ = false;

        ~promise_type()
        {
            // Still owned only when a closed executor destroyed this
            // trampoline instead of resuming it to unwind the task.
            if(task_h_)
                task_h_.destroy();
        }

        promise_type(Ex& ex, Handlers& h, Alloc& a) noexcept
            : wg_(std::move(ex))
            , handlers_(std::move(h))
            , resource_(std::move(a))
        {
        }

        // Allocates whole frame units: a standard allocator takes no
        // alignment argument, and one rebound to std::byte may place
        // this frame at byte alignment.
        using unit_alloc = typename std::allocator_traits<Alloc>
            ::template rebind_alloc<frame_unit>;

        static_assert(
            alignof(unit_alloc) <= frame_alignment,
            "Alloc must not be over-aligned");

        static constexpr std::size_t
        round_up(std::size_t n, std::size_t align) noexcept
        {
            return (n + align - 1) & ~(align - 1);
        }

        // The block holds the frame, then the deallocator, then the
        // allocator copy, each at its own alignment. The copy comes
        // last, where dealloc_impl looks for it.
        static constexpr std::size_t
        fn_offset(std::size_t size) noexcept
        {
            return round_up(size, alignof(dealloc_fn));
        }

        static constexpr std::size_t
        block_size(std::size_t size) noexcept
        {
            return round_up(
                fn_offset(size) + sizeof(dealloc_fn),
                alignof(unit_alloc)) + sizeof(unit_alloc);
        }

        static void* operator new(
            std::size_t size, Ex const&, Handlers const&, Alloc a)
        {
            auto const total = block_size(size);

            unit_alloc ua(std::move(a));
            void* raw = ua.allocate(frame_units(total));

            auto* fn_loc = reinterpret_cast<dealloc_fn*>(
                static_cast<char*>(raw) + fn_offset(size));
            *fn_loc = &dealloc_impl<unit_alloc>;

            new (static_cast<char*>(raw) + total - sizeof(unit_alloc))
                unit_alloc(std::move(ua));

            return raw;
        }

        static void operator delete(void* ptr, std::size_t size)
        {
            auto* fn = reinterpret_cast<dealloc_fn*>(
                static_cast<char*>(ptr) + fn_offset(size));
            (*fn)(ptr, block_size(size));
        }

        std::pmr::memory_resource* get_resource() noexcept
        {
            return &resource_;
        }

        run_async_trampoline get_return_object() noexcept
        {
            return run_async_trampoline{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_never final_suspend() noexcept
        {
            return {};
        }

        void return_void() noexcept
        {
        }

        // An exception reaches here only by escaping a handler: a handler
        // that threw, or the default handler rethrowing an otherwise
        // unhandled task exception. default_handler drops a quitter's
        // stop, so this is always a genuine error with no owner to
        // receive it: fail fast.
        void unhandled_exception() noexcept { std::terminate(); } // LCOV_EXCL_LINE
    };

    std::coroutine_handle<promise_type> h_;

    template<ContextRunnable Task>
    static void invoke_impl(void* p, Handlers& h, bool stopped)
    {
        if constexpr(stops_by_abandonment<Task>)
        {
            if(stopped)
            {
                invoke_stopped<Task>(h);
                return;
            }
        }
        (void)stopped;
        using R = decltype(std::declval<Task&>().await_resume());
        auto& promise = *static_cast<typename Task::promise_type*>(p);
        if(promise.exception())
            h(promise.exception());
        else if constexpr(std::is_void_v<R>)
            h();
        else
            h(std::move(promise.result()));
    }
};

/** Specialization for memory_resource* - stores pointer directly.

    This avoids double indirection when the user passes a memory_resource*.
*/
template<class Ex, class Handlers>
struct run_async_trampoline<Ex, Handlers, std::pmr::memory_resource*>
{
    using invoke_fn = void(*)(void*, Handlers&, bool);

    struct promise_type
    {
        work_guard<Ex> wg_;
        Handlers handlers_;
        std::pmr::memory_resource* mr_;
        await_context env_;
        invoke_fn invoke_ = nullptr;
        void* task_promise_ = nullptr;
        // task_h_: raw handle for frame_guard cleanup in make_trampoline.
        // task_cont_: continuation dispatched to start the task: the same
        // handle, or for a quitter the gate that starts it.
        std::coroutine_handle<> task_h_;
        continuation task_cont_;
        // Owns the task's segment: abandoning it resumes this
        // trampoline, or has it destroyed, to unwind the task.
        chain_owner owner_;
        // The task reports a stop-caused abandon (a quitter). For any
        // other task such an abandon is torn down like any other.
        bool stoppable_ = false;

        ~promise_type()
        {
            // Still owned only when a closed executor destroyed this
            // trampoline instead of resuming it to unwind the task.
            if(task_h_)
                task_h_.destroy();
        }

        promise_type(
            Ex& ex, Handlers& h, std::pmr::memory_resource* mr) noexcept
            : wg_(std::move(ex))
            , handlers_(std::move(h))
            , mr_(mr)
        {
        }

        static void* operator new(
            std::size_t size, Ex const&, Handlers const&,
            std::pmr::memory_resource* mr)
        {
            auto total = size + sizeof(mr);
            void* raw = mr->allocate(total, frame_alignment);
            std::memcpy(static_cast<char*>(raw) + size, &mr, sizeof(mr));
            return raw;
        }

        static void operator delete(void* ptr, std::size_t size)
        {
            std::pmr::memory_resource* mr;
            std::memcpy(&mr, static_cast<char*>(ptr) + size, sizeof(mr));
            auto total = size + sizeof(mr);
            mr->deallocate(ptr, total, frame_alignment);
        }

        std::pmr::memory_resource* get_resource() noexcept
        {
            return mr_;
        }

        run_async_trampoline get_return_object() noexcept
        {
            return run_async_trampoline{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_never final_suspend() noexcept
        {
            return {};
        }

        void return_void() noexcept
        {
        }

        // See primary template: an escaping handler exception is fatal.
        void unhandled_exception() noexcept { std::terminate(); } // LCOV_EXCL_LINE
    };

    std::coroutine_handle<promise_type> h_;

    template<ContextRunnable Task>
    static void invoke_impl(void* p, Handlers& h, bool stopped)
    {
        if constexpr(stops_by_abandonment<Task>)
        {
            if(stopped)
            {
                invoke_stopped<Task>(h);
                return;
            }
        }
        (void)stopped;
        using R = decltype(std::declval<Task&>().await_resume());
        auto& promise = *static_cast<typename Task::promise_type*>(p);
        if(promise.exception())
            h(promise.exception());
        else if constexpr(std::is_void_v<R>)
            h();
        else
            h(std::move(promise.result()));
    }
};
BOOST_CAPY_MSVC_WARNING_POP

/// Coroutine body for run_async_trampoline - invokes handlers then destroys task.
template<class Ex, class Handlers, class Alloc>
run_async_trampoline<Ex, Handlers, Alloc>
make_trampoline(Ex, Handlers, Alloc)
{
    // promise_type ctor steals the parameters
    auto& p = co_await get_promise_awaiter<
        typename run_async_trampoline<Ex, Handlers, Alloc>::promise_type>{};

    // Guard ensures the task frame is destroyed even when invoke_
    // throws (e.g. default_handler rethrows an unhandled exception).
    struct frame_guard
    {
        std::coroutine_handle<>& h;
        ~frame_guard()
        {
            if(h)
                std::exchange(h, nullptr).destroy();
        }
    } guard{p.task_h_};

    // Resumed with the task unfinished: the segment was abandoned. A
    // teardown destroys the task without calling a handler; a quitter's
    // stop reports its stopped value.
    // A stopped quitter's frames are destroyed before its handler runs,
    // as a finished task's destructors have run by then.
    // Only a quitter run by this trampoline is stopped through its
    // owner. A quitter driven through its handle() in a task's segment
    // would tear the task down here with no handler called.
    BOOST_CAPY_ASSERT((p.stoppable_ || !p.owner_.stopped()) &&
        "a quitter stopped a task's segment: see quitter::handle");
    bool const stopped = p.stoppable_ && p.owner_.stopped();
    if(stopped)
        std::exchange(p.task_h_, nullptr).destroy();
    if(!p.owner_.abandoned() || stopped)
        p.invoke_(p.task_promise_, p.handlers_, stopped);
}

} // namespace detail

/** Installs the frame allocator, then starts the task on the executor when called once.

    This wrapper holds the run_async_trampoline coroutine, executor, stop token,
    and handlers. The run_async_trampoline is allocated when the wrapper is constructed
    (before the task due to C++17 postfix evaluation order).

    The rvalue ref-qualifier on `operator()` ensures the wrapper can only
    be used as a temporary, preventing misuse that would violate LIFO ordering.

    @tparam Ex The executor type satisfying the `Executor` concept.
    @tparam Handlers The handler type (default_handler or handler_pair).
    @tparam Alloc The allocator type (value type or memory_resource*).

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @warning **Always construct the task as the direct argument of the
    two-call expression `run_async(ex)(task)`.** The wrapper's constructor
    installs the frame allocator in thread-local storage. The task's
    `operator new` reads that thread-local state. Splitting the two calls
    apart in any of the following ways allocates the task's coroutine
    frame under the wrong allocator. Each does so silently, with no
    compile error.
    @li *Stored wrapper.* Storing the wrapper itself
        (`auto w = run_async(ex);`) compiles fine. C++17 guaranteed copy
        elision constructs `w` directly from the prvalue. The deleted
        copy/move constructors are never considered. What the rvalue
        ref-qualifier on `operator()` rejects is calling through that
        stored lvalue: `w(my_task())` does not compile, and
        `std::move(w)(my_task())` is required instead. The silent
        variant is storing the *task*
        (`auto t = my_task(); run_async(ex)(std::move(t));`): `t`'s frame
        is allocated before `run_async(ex)` ever runs.
    @li *Preconstructed task.* Passing an already-constructed task object
        has the same effect as the stored-wrapper case. So does passing a
        moved-from local, or a task returned from an earlier statement.
        The frame exists before the allocator is installed.
    @li *Wrapper function.* Forwarding the task through a helper that
        itself performs the two-call pattern constructs the task as an
        argument to the helper. It is therefore constructed before the
        helper's body runs, and so before `run_async` runs. An example is
        `submit(ex, my_task())`, where `submit` calls
        `run_async(ex)(std::forward<Task>(t))` internally.

    See the Frame Allocators guide
    (`doc/modules/ROOT/pages/4.coroutines/4g.allocators.adoc`) for the full
    C++17-evaluation-order rationale behind this constraint.

    @par Example
    @par !example example


    @see @ref run_async
*/
template<Executor Ex, class Handlers, class Alloc>
class [[nodiscard]] run_async_wrapper
{
    detail::run_async_trampoline<Ex, Handlers, Alloc> tr_;
    std::stop_token st_;
    std::pmr::memory_resource* saved_tls_;

public:
    /** Construct the wrapper and install the frame allocator.

        Builds the trampoline and saves the current thread-local frame
        allocator. Then installs the trampoline's resource as the new
        thread-local allocator. The task frame, evaluated as the argument
        to @ref operator(), is therefore allocated from that resource.

        @param ex The executor on which the task runs.
        @param st The stop token for cooperative cancellation.
        @param h The completion handlers.
        @param a The allocator for frame allocation.

        @note When `Alloc` is not `std::pmr::memory_resource*` it must be
        nothrow move constructible (enforced by a `static_assert`), which
        is what allows this constructor to be `noexcept`. If allocating
        the launch state throws, `std::terminate` is called.
    */
    run_async_wrapper(
        Ex ex,
        std::stop_token st,
        Handlers h,
        Alloc a) noexcept
        : tr_(detail::make_trampoline<Ex, Handlers, Alloc>(
            std::move(ex), std::move(h), std::move(a)))
        , st_(std::move(st))
        , saved_tls_(get_current_frame_allocator())
    {
        if constexpr (!std::is_same_v<Alloc, std::pmr::memory_resource*>)
        {
            static_assert(
                std::is_nothrow_move_constructible_v<Alloc>,
                "Allocator must be nothrow move constructible");
        }
        // Set TLS before task argument is evaluated
        set_current_frame_allocator(tr_.h_.promise().get_resource());
    }

    /** Restore the previously installed frame allocator.

        Resets the thread-local frame allocator to the value saved at
        construction. A stale pointer to the trampoline's resource
        therefore does not outlive the execution context that owns it.
        If no task was started, for example because evaluating the task
        argument threw, the unused launch state is destroyed, releasing
        the executor's work.
    */
    ~run_async_wrapper()
    {
        if(tr_.h_)
            tr_.h_.destroy();
        set_current_frame_allocator(saved_tls_);
    }

    // Non-copyable, non-movable (must be used immediately)

    /** Copy construction is disabled; the wrapper must be used immediately.

        @param other The wrapper that would be copied.
    */
    run_async_wrapper(run_async_wrapper const& other) = delete;

    /** Move construction is disabled; the wrapper must be used immediately.

        @param other The wrapper that would be moved from.
    */
    run_async_wrapper(run_async_wrapper&& other) = delete;

    /** Copy assignment is disabled; the wrapper must be used immediately.

        @param other The wrapper that would be assigned from.

        @return A reference to `*this`.
    */
    run_async_wrapper& operator=(run_async_wrapper const& other) = delete;

    /** Move assignment is disabled; the wrapper must be used immediately.

        @param other The wrapper that would be moved from.

        @return A reference to `*this`.
    */
    run_async_wrapper& operator=(run_async_wrapper&& other) = delete;

    /** Start the task for execution.

        This operator accepts a task and starts it on the executor.
        The rvalue ref-qualifier ensures the wrapper is consumed, enforcing
        correct LIFO destruction order.

        The `await_context` constructed for the task is owned by the trampoline
        coroutine and is guaranteed to outlive the task and all awaitables
        in its chain. Awaitables may store `await_context const*` without concern
        for dangling references.

        @throws Any exception thrown by the executor's `dispatch`, or
            `std::bad_alloc` if the gate of a @ref quitter cannot be
            allocated. The task is then destroyed without running, and
            the executor's work is released.

        @tparam Task The ContextRunnable type.

        @param t The task to execute. Ownership is transferred to the
                 run_async_trampoline which destroys it after completion.
    */
    template<ContextRunnable Task>
    void operator()(Task t) &&
    {
        auto task_h = t.handle();
        auto& task_promise = task_h.promise();
        t.release();

        auto& p = tr_.h_.promise();

        // Inject Task-specific invoke function
        p.invoke_ = detail::run_async_trampoline<Ex, Handlers, Alloc>::template invoke_impl<Task>;
        p.task_promise_ = &task_promise;
        p.task_h_ = task_h;
        p.stoppable_ = detail::stops_by_abandonment<Task>;

        // Setup task's continuation to return to run_async_trampoline
        task_promise.set_continuation(tr_.h_);
        p.owner_.bind(tr_.h_, p.wg_.executor());
        p.env_ = {p.wg_.executor(), st_, p.get_resource(), &p.owner_};
        task_promise.set_environment(&p.env_);

        // Start task through executor.
        // safe_resume is not needed here: TLS is already saved in the
        // constructor (saved_tls_) and restored in the destructor.
        p.task_cont_.h = task_h;
        p.task_cont_.owner = &p.owner_;

        // A quitter starts through its gate, which checks the stop when
        // the body would start: a stopped quitter's segment is abandoned
        // for the stop, and the trampoline reports the stopped value.
        // If this or dispatch throws, the destructor destroys the
        // trampoline and the unstarted task with it.
        if constexpr(detail::stops_by_abandonment<Task>)
            p.task_cont_.h = task_promise.launch_handle();
        auto next = p.wg_.executor().dispatch(p.task_cont_);
        // From here the trampoline destroys itself when the task ends.
        tr_.h_ = nullptr;
        next.resume();
    }
};

// Executor only (uses default recycling allocator)

/** Bind an executor to produce a launcher. Invoke the launcher with a task to start it.

    Use this to start execution of a `task<T>` that was created lazily.
    The returned wrapper must be immediately invoked with the task;
    storing the wrapper and calling it later violates LIFO ordering.

    Uses the default recycling frame allocator for coroutine frames.
    With no handlers, the result is discarded. An unhandled exception
    thrown by the task calls `std::terminate`. To catch it instead, pass
    an error handler that receives it as an `exception_ptr`, or `co_await`
    the work inside a coroutine. A stopped @ref quitter whose `T` is not
    `results` or `std::error_code` reports its stop to the error
    handler as a `std::system_error` whose `code()` compares equal to
    `cond::canceled`. With no error handler, that stop is discarded,
    also when it escapes a task that awaited the quitter.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread.

    @par Example
    @par !example example_1


    @param ex The executor to execute the task on.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex>
[[nodiscard]] auto
run_async(Ex ex)
{
    auto* mr = ex.context().get_frame_allocator();
    return run_async_wrapper<Ex, detail::default_handler, std::pmr::memory_resource*>(
        std::move(ex),
        std::stop_token{},
        detail::default_handler{},
        mr);
}

/** Bind an executor and a result handler to produce a launcher. Invoke the launcher with a task to start it.

    The handler `h1` is called with the task's result on success. If `h1`
    is also invocable with `std::exception_ptr`, it handles exceptions too.
    Otherwise, an unhandled exception calls `std::terminate`, except a
    @ref quitter's stop, which is discarded. That includes a stop that
    escapes a task that awaited the quitter.
    If the task is abandoned before it completes, as a context's teardown
    does, no handler is called; the handlers are destroyed with it.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @par Example
    @par !example example_2


    @param ex The executor to execute the task on.
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, class H1>
    requires detail::RunAsyncHandler<H1>
[[nodiscard]] auto
run_async(Ex ex, H1 h1)
{
    auto* mr = ex.context().get_frame_allocator();
    return run_async_wrapper<Ex, detail::handler_pair<H1, detail::default_handler>, std::pmr::memory_resource*>(
        std::move(ex),
        std::stop_token{},
        detail::handler_pair<H1, detail::default_handler>{std::move(h1)},
        mr);
}

/** Bind an executor and separate result and error handlers to produce a launcher. Invoke the launcher with a task to start it.

    The handler `h1` is called with the task's result on success.
    The handler `h2` is called with the exception_ptr on failure.
    If the task is abandoned before it completes, as a context's teardown
    does, no handler is called; the handlers are destroyed with it.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @par Example
    @par !example example_3


    @param ex The executor to execute the task on.
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, class H1, class H2>
    requires (detail::RunAsyncHandler<H1> && detail::RunAsyncHandler<H2>)
[[nodiscard]] auto
run_async(Ex ex, H1 h1, H2 h2)
{
    auto* mr = ex.context().get_frame_allocator();
    return run_async_wrapper<Ex, detail::handler_pair<H1, H2>, std::pmr::memory_resource*>(
        std::move(ex),
        std::stop_token{},
        detail::handler_pair<H1, H2>{std::move(h1), std::move(h2)},
        mr);
}

// Ex + stop_token

/** Bind an executor and a stop token to produce a launcher. Invoke the launcher with a task to start it.

    The stop token is propagated to the task, enabling cooperative
    cancellation. With no handlers, the result is discarded and an
    unhandled exception calls `std::terminate`. A stopped @ref quitter
    reports its stopped value to the handlers: see @ref quitter.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread.

    @par Example
    @par !example example_4


    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex>
[[nodiscard]] auto
run_async(Ex ex, std::stop_token st)
{
    auto* mr = ex.context().get_frame_allocator();
    return run_async_wrapper<Ex, detail::default_handler, std::pmr::memory_resource*>(
        std::move(ex),
        std::move(st),
        detail::default_handler{},
        mr);
}

/** Bind an executor, a stop token, and a result handler to produce a launcher. Invoke the launcher with a task to start it.

    The stop token is propagated to the task for cooperative cancellation.
    The handler `h1` is called with the result on success, and optionally
    with exception_ptr if it accepts that type.

    A stopped @ref quitter reports its stopped value to the handlers:
    see @ref quitter.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, class H1>
    requires detail::RunAsyncHandler<H1>
[[nodiscard]] auto
run_async(Ex ex, std::stop_token st, H1 h1)
{
    auto* mr = ex.context().get_frame_allocator();
    return run_async_wrapper<Ex, detail::handler_pair<H1, detail::default_handler>, std::pmr::memory_resource*>(
        std::move(ex),
        std::move(st),
        detail::handler_pair<H1, detail::default_handler>{std::move(h1)},
        mr);
}

/** Bind an executor, a stop token, and separate result and error handlers to produce a launcher. Invoke the launcher with a task to start it.

    The stop token is propagated to the task for cooperative cancellation.
    The handler `h1` is called on success, `h2` on failure.

    A stopped @ref quitter reports its stopped value to the handlers:
    see @ref quitter.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, class H1, class H2>
    requires (detail::RunAsyncHandler<H1> && detail::RunAsyncHandler<H2>)
[[nodiscard]] auto
run_async(Ex ex, std::stop_token st, H1 h1, H2 h2)
{
    auto* mr = ex.context().get_frame_allocator();
    return run_async_wrapper<Ex, detail::handler_pair<H1, H2>, std::pmr::memory_resource*>(
        std::move(ex),
        std::move(st),
        detail::handler_pair<H1, H2>{std::move(h1), std::move(h2)},
        mr);
}

// Ex + memory_resource*

/** Bind an executor and a memory resource to produce a launcher. Invoke the launcher with a task to start it.

    The memory resource is used for coroutine frame allocation.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread.

    @pre `mr` outlives every task started through the returned wrapper.

    @param ex The executor to execute the task on.
    @param mr The memory resource for frame allocation.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex>
[[nodiscard]] auto
run_async(Ex ex, std::pmr::memory_resource* mr)
{
    return run_async_wrapper<Ex, detail::default_handler, std::pmr::memory_resource*>(
        std::move(ex),
        std::stop_token{},
        detail::default_handler{},
        mr);
}

/** Bind an executor, a memory resource, and a result handler to produce a launcher. Invoke the launcher with a task to start it.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @pre `mr` outlives every task started through the returned wrapper.

    @param ex The executor to execute the task on.
    @param mr The memory resource for frame allocation.
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, class H1>
[[nodiscard]] auto
run_async(Ex ex, std::pmr::memory_resource* mr, H1 h1)
{
    return run_async_wrapper<Ex, detail::handler_pair<H1, detail::default_handler>, std::pmr::memory_resource*>(
        std::move(ex),
        std::stop_token{},
        detail::handler_pair<H1, detail::default_handler>{std::move(h1)},
        mr);
}

/** Bind an executor, a memory resource, and separate result and error handlers to produce a launcher. Invoke the launcher with a task to start it.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @pre `mr` outlives every task started through the returned wrapper.

    @param ex The executor to execute the task on.
    @param mr The memory resource for frame allocation.
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, class H1, class H2>
[[nodiscard]] auto
run_async(Ex ex, std::pmr::memory_resource* mr, H1 h1, H2 h2)
{
    return run_async_wrapper<Ex, detail::handler_pair<H1, H2>, std::pmr::memory_resource*>(
        std::move(ex),
        std::stop_token{},
        detail::handler_pair<H1, H2>{std::move(h1), std::move(h2)},
        mr);
}

// Ex + stop_token + memory_resource*

/** Bind an executor, a stop token, and a memory resource to produce a launcher. Invoke the launcher with a task to start it.

    A stopped @ref quitter reports its stopped value to the handlers:
    see @ref quitter.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread.

    @pre `mr` outlives every task started through the returned wrapper.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param mr The memory resource for frame allocation.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex>
[[nodiscard]] auto
run_async(Ex ex, std::stop_token st, std::pmr::memory_resource* mr)
{
    return run_async_wrapper<Ex, detail::default_handler, std::pmr::memory_resource*>(
        std::move(ex),
        std::move(st),
        detail::default_handler{},
        mr);
}

/** Bind an executor, a stop token, a memory resource, and a result handler to produce a launcher. Invoke the launcher with a task to start it.

    A stopped @ref quitter reports its stopped value to the handlers:
    see @ref quitter.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @pre `mr` outlives every task started through the returned wrapper.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param mr The memory resource for frame allocation.
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, class H1>
[[nodiscard]] auto
run_async(Ex ex, std::stop_token st, std::pmr::memory_resource* mr, H1 h1)
{
    return run_async_wrapper<Ex, detail::handler_pair<H1, detail::default_handler>, std::pmr::memory_resource*>(
        std::move(ex),
        std::move(st),
        detail::handler_pair<H1, detail::default_handler>{std::move(h1)},
        mr);
}

/** Bind an executor, a stop token, a memory resource, and separate result and error handlers to produce a launcher. Invoke the launcher with a task to start it.

    A stopped @ref quitter reports its stopped value to the handlers:
    see @ref quitter.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @pre `mr` outlives every task started through the returned wrapper.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param mr The memory resource for frame allocation.
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, class H1, class H2>
[[nodiscard]] auto
run_async(Ex ex, std::stop_token st, std::pmr::memory_resource* mr, H1 h1, H2 h2)
{
    return run_async_wrapper<Ex, detail::handler_pair<H1, H2>, std::pmr::memory_resource*>(
        std::move(ex),
        std::move(st),
        detail::handler_pair<H1, H2>{std::move(h1), std::move(h2)},
        mr);
}

// Ex + standard Allocator (value type)

/** Bind an executor and an allocator to produce a launcher. Invoke the launcher with a task to start it.

    The allocator is wrapped in a frame_memory_resource and stored in the
    run_async_trampoline, ensuring it outlives all coroutine frames.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread.

    @param ex The executor to execute the task on.
    @param alloc The allocator for frame allocation (copied and stored).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, detail::Allocator Alloc>
[[nodiscard]] auto
run_async(Ex ex, Alloc alloc)
{
    return run_async_wrapper<Ex, detail::default_handler, Alloc>(
        std::move(ex),
        std::stop_token{},
        detail::default_handler{},
        std::move(alloc));
}

/** Bind an executor, an allocator, and a result handler to produce a launcher. Invoke the launcher with a task to start it.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @param ex The executor to execute the task on.
    @param alloc The allocator for frame allocation (copied and stored).
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, detail::Allocator Alloc, class H1>
[[nodiscard]] auto
run_async(Ex ex, Alloc alloc, H1 h1)
{
    return run_async_wrapper<Ex, detail::handler_pair<H1, detail::default_handler>, Alloc>(
        std::move(ex),
        std::stop_token{},
        detail::handler_pair<H1, detail::default_handler>{std::move(h1)},
        std::move(alloc));
}

/** Bind an executor, an allocator, and separate result and error handlers to produce a launcher. Invoke the launcher with a task to start it.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @param ex The executor to execute the task on.
    @param alloc The allocator for frame allocation (copied and stored).
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, detail::Allocator Alloc, class H1, class H2>
[[nodiscard]] auto
run_async(Ex ex, Alloc alloc, H1 h1, H2 h2)
{
    return run_async_wrapper<Ex, detail::handler_pair<H1, H2>, Alloc>(
        std::move(ex),
        std::stop_token{},
        detail::handler_pair<H1, H2>{std::move(h1), std::move(h2)},
        std::move(alloc));
}

// Ex + stop_token + standard Allocator

/** Bind an executor, a stop token, and an allocator to produce a launcher. Invoke the launcher with a task to start it.

    A stopped @ref quitter reports its stopped value to the handlers:
    see @ref quitter.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param alloc The allocator for frame allocation (copied and stored).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, detail::Allocator Alloc>
[[nodiscard]] auto
run_async(Ex ex, std::stop_token st, Alloc alloc)
{
    return run_async_wrapper<Ex, detail::default_handler, Alloc>(
        std::move(ex),
        std::move(st),
        detail::default_handler{},
        std::move(alloc));
}

/** Bind an executor, a stop token, an allocator, and a result handler to produce a launcher. Invoke the launcher with a task to start it.

    A stopped @ref quitter reports its stopped value to the handlers:
    see @ref quitter.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param alloc The allocator for frame allocation (copied and stored).
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, detail::Allocator Alloc, class H1>
[[nodiscard]] auto
run_async(Ex ex, std::stop_token st, Alloc alloc, H1 h1)
{
    return run_async_wrapper<Ex, detail::handler_pair<H1, detail::default_handler>, Alloc>(
        std::move(ex),
        std::move(st),
        detail::handler_pair<H1, detail::default_handler>{std::move(h1)},
        std::move(alloc));
}

/** Bind an executor, a stop token, an allocator, and separate result and error handlers to produce a launcher. Invoke the launcher with a task to start it.

    A stopped @ref quitter reports its stopped value to the handlers:
    see @ref quitter.

    Construct the task as the direct argument of the two-call expression
    `run_async(ex)(task)`.

    @par Thread Safety
    The wrapper itself should only be used from one thread. The handlers
    may be invoked from any thread where the executor schedules work.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param alloc The allocator for frame allocation (copied and stored).
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see @ref task
    @see @ref Executor
    @see @ref run_async_wrapper
*/
template<Executor Ex, detail::Allocator Alloc, class H1, class H2>
[[nodiscard]] auto
run_async(Ex ex, std::stop_token st, Alloc alloc, H1 h1, H2 h2)
{
    return run_async_wrapper<Ex, detail::handler_pair<H1, H2>, Alloc>(
        std::move(ex),
        std::move(st),
        detail::handler_pair<H1, H2>{std::move(h1), std::move(h2)},
        std::move(alloc));
}

} // namespace capy
} // namespace boost

#endif
