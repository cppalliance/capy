//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
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
#include <boost/capy/concept/io_runnable.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/recycling_memory_resource.hpp>
#include <boost/capy/ex/work_guard.hpp>

#include <algorithm>
#include <coroutine>
#include <cstring>
#include <exception>
#include <memory_resource>
#include <new>
#include <stop_token>
#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

/// Function pointer type for type-erased frame deallocation.
using dealloc_fn = void(*)(void*, std::size_t);

/// Type-erased deallocator implementation for trampoline frames.
template<class Alloc>
void dealloc_impl(void* raw, std::size_t total)
{
    static_assert(std::is_same_v<typename Alloc::value_type, std::byte>);
    auto* a = std::launder(reinterpret_cast<Alloc*>(
        static_cast<char*>(raw) + total - sizeof(Alloc)));
    Alloc ba(std::move(*a));
    a->~Alloc();
    ba.deallocate(static_cast<std::byte*>(raw), total);
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
struct BOOST_CAPY_CORO_DESTROY_WHEN_COMPLETE run_async_trampoline
{
    using invoke_fn = void(*)(void*, Handlers&);

    struct promise_type
    {
        work_guard<Ex> wg_;
        Handlers handlers_;
        frame_memory_resource<Alloc> resource_;
        io_env env_;
        invoke_fn invoke_ = nullptr;
        void* task_promise_ = nullptr;
        // task_h_: raw handle for frame_guard cleanup in make_trampoline.
        // task_cont_: continuation wrapping the same handle for executor dispatch.
        // Both must reference the same coroutine and be kept in sync.
        std::coroutine_handle<> task_h_;
        continuation task_cont_;

        promise_type(Ex& ex, Handlers& h, Alloc& a) noexcept
            : wg_(std::move(ex))
            , handlers_(std::move(h))
            , resource_(std::move(a))
        {
        }

        static void* operator new(
            std::size_t size, Ex const&, Handlers const&, Alloc a)
        {
            using byte_alloc = typename std::allocator_traits<Alloc>
                ::template rebind_alloc<std::byte>;

            constexpr auto footer_align =
                (std::max)(alignof(dealloc_fn), alignof(Alloc));
            auto padded = (size + footer_align - 1) & ~(footer_align - 1);
            auto total = padded + sizeof(dealloc_fn) + sizeof(Alloc);

            byte_alloc ba(std::move(a));
            void* raw = ba.allocate(total);

            auto* fn_loc = reinterpret_cast<dealloc_fn*>(
                static_cast<char*>(raw) + padded);
            *fn_loc = &dealloc_impl<byte_alloc>;

            new (fn_loc + 1) byte_alloc(std::move(ba));

            return raw;
        }

        static void operator delete(void* ptr, std::size_t size)
        {
            constexpr auto footer_align =
                (std::max)(alignof(dealloc_fn), alignof(Alloc));
            auto padded = (size + footer_align - 1) & ~(footer_align - 1);
            auto total = padded + sizeof(dealloc_fn) + sizeof(Alloc);

            auto* fn = reinterpret_cast<dealloc_fn*>(
                static_cast<char*>(ptr) + padded);
            (*fn)(ptr, total);
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
        // unhandled task exception. Cancellation is filtered out earlier
        // by default_handler, so this is always a genuine error with no
        // owner to receive it: fail fast.
        void unhandled_exception() noexcept { std::terminate(); } // LCOV_EXCL_LINE
    };

    std::coroutine_handle<promise_type> h_;

    template<IoRunnable Task>
    static void invoke_impl(void* p, Handlers& h)
    {
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
struct BOOST_CAPY_CORO_DESTROY_WHEN_COMPLETE
    run_async_trampoline<Ex, Handlers, std::pmr::memory_resource*>
{
    using invoke_fn = void(*)(void*, Handlers&);

    struct promise_type
    {
        work_guard<Ex> wg_;
        Handlers handlers_;
        std::pmr::memory_resource* mr_;
        io_env env_;
        invoke_fn invoke_ = nullptr;
        void* task_promise_ = nullptr;
        // task_h_: raw handle for frame_guard cleanup in make_trampoline.
        // task_cont_: continuation wrapping the same handle for executor dispatch.
        // Both must reference the same coroutine and be kept in sync.
        std::coroutine_handle<> task_h_;
        continuation task_cont_;

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
            void* raw = mr->allocate(total, alignof(std::max_align_t));
            std::memcpy(static_cast<char*>(raw) + size, &mr, sizeof(mr));
            return raw;
        }

        static void operator delete(void* ptr, std::size_t size)
        {
            std::pmr::memory_resource* mr;
            std::memcpy(&mr, static_cast<char*>(ptr) + size, sizeof(mr));
            auto total = size + sizeof(mr);
            mr->deallocate(ptr, total, alignof(std::max_align_t));
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

    template<IoRunnable Task>
    static void invoke_impl(void* p, Handlers& h)
    {
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
        ~frame_guard() { h.destroy(); }
    } guard{p.task_h_};

    p.invoke_(p.task_promise_, p.handlers_);
}

} // namespace detail

/** Wrapper returned by run_async that accepts a task for execution.

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

    @par Example
    @code
    // Correct usage - wrapper is temporary
    run_async(ex)(my_task());

    // Compile error - cannot call operator() on lvalue
    auto w = run_async(ex);
    w(my_task());  // Error: operator() requires rvalue
    @endcode

    @see run_async
*/
template<Executor Ex, class Handlers, class Alloc>
class [[nodiscard]] run_async_wrapper
{
    detail::run_async_trampoline<Ex, Handlers, Alloc> tr_;
    std::stop_token st_;
    std::pmr::memory_resource* saved_tls_;

public:
    /// Construct wrapper with executor, stop token, handlers, and allocator.
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

    ~run_async_wrapper()
    {
        // Restore TLS so stale pointer doesn't outlive
        // the execution context that owns the resource.
        set_current_frame_allocator(saved_tls_);
    }

    // Non-copyable, non-movable (must be used immediately)
    run_async_wrapper(run_async_wrapper const&) = delete;
    run_async_wrapper(run_async_wrapper&&) = delete;
    run_async_wrapper& operator=(run_async_wrapper const&) = delete;
    run_async_wrapper& operator=(run_async_wrapper&&) = delete;

    /** Launch the task for execution.

        This operator accepts a task and launches it on the executor.
        The rvalue ref-qualifier ensures the wrapper is consumed, enforcing
        correct LIFO destruction order.

        The `io_env` constructed for the task is owned by the trampoline
        coroutine and is guaranteed to outlive the task and all awaitables
        in its chain. Awaitables may store `io_env const*` without concern
        for dangling references.

        @tparam Task The IoRunnable type.

        @param t The task to execute. Ownership is transferred to the
                 run_async_trampoline which will destroy it after completion.
    */
    template<IoRunnable Task>
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

        // Setup task's continuation to return to run_async_trampoline
        task_promise.set_continuation(tr_.h_);
        p.env_ = {p.wg_.executor(), st_, p.get_resource()};
        task_promise.set_environment(&p.env_);

        // Start task through executor.
        // safe_resume is not needed here: TLS is already saved in the
        // constructor (saved_tls_) and restored in the destructor.
        p.task_cont_.h = task_h;
        p.wg_.executor().dispatch(p.task_cont_).resume();
    }
};

// Executor only (uses default recycling allocator)

/** Asynchronously launch a lazy task on the given executor.

    Use this to start execution of a `task<T>` that was created lazily.
    The returned wrapper must be immediately invoked with the task;
    storing the wrapper and calling it later violates LIFO ordering.

    Uses the default recycling frame allocator for coroutine frames.
    With no handlers, the result is discarded. An unhandled exception
    thrown by the task calls `std::terminate`; pass an error handler to
    receive it as an `exception_ptr`, or `co_await` the work inside a
    coroutine if you want to catch it.

    @par Thread Safety
    The wrapper and handlers may be called from any thread where the
    executor schedules work.

    @par Example
    @code
    run_async(ioc.get_executor())(my_task());
    @endcode

    @param ex The executor to execute the task on.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with a result handler.

    The handler `h1` is called with the task's result on success. If `h1`
    is also invocable with `std::exception_ptr`, it handles exceptions too.
    Otherwise, an unhandled exception calls `std::terminate`.

    @par Thread Safety
    The handler may be called from any thread where the executor
    schedules work.

    @par Example
    @code
    // Handler for result only (exceptions rethrown)
    run_async(ex, [](int result) {
        std::cout << "Got: " << result << "\n";
    })(compute_value());

    // Overloaded handler for both result and exception
    run_async(ex, overloaded{
        [](int result) { std::cout << "Got: " << result << "\n"; },
        [](std::exception_ptr) { std::cout << "Failed\n"; }
    })(compute_value());
    @endcode

    @param ex The executor to execute the task on.
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
*/
template<Executor Ex, class H1>
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

/** Asynchronously launch a lazy task with separate result and error handlers.

    The handler `h1` is called with the task's result on success.
    The handler `h2` is called with the exception_ptr on failure.

    @par Thread Safety
    The handlers may be called from any thread where the executor
    schedules work.

    @par Example
    @code
    run_async(ex,
        [](int result) { std::cout << "Got: " << result << "\n"; },
        [](std::exception_ptr ep) {
            try { std::rethrow_exception(ep); }
            catch (std::exception const& e) {
                std::cout << "Error: " << e.what() << "\n";
            }
        }
    )(compute_value());
    @endcode

    @param ex The executor to execute the task on.
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
*/
template<Executor Ex, class H1, class H2>
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

/** Asynchronously launch a lazy task with stop token support.

    The stop token is propagated to the task, enabling cooperative
    cancellation. With no handlers, the result is discarded and an
    unhandled exception calls `std::terminate`.

    @par Thread Safety
    The wrapper may be called from any thread where the executor
    schedules work.

    @par Example
    @code
    std::stop_source source;
    run_async(ex, source.get_token())(cancellable_task());
    // Later: source.request_stop();
    @endcode

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with stop token and result handler.

    The stop token is propagated to the task for cooperative cancellation.
    The handler `h1` is called with the result on success, and optionally
    with exception_ptr if it accepts that type.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
*/
template<Executor Ex, class H1>
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

/** Asynchronously launch a lazy task with stop token and separate handlers.

    The stop token is propagated to the task for cooperative cancellation.
    The handler `h1` is called on success, `h2` on failure.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
*/
template<Executor Ex, class H1, class H2>
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

/** Asynchronously launch a lazy task with custom memory resource.

    The memory resource is used for coroutine frame allocation. The caller
    is responsible for ensuring the memory resource outlives all tasks.

    @param ex The executor to execute the task on.
    @param mr The memory resource for frame allocation.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with memory resource and handler.

    @param ex The executor to execute the task on.
    @param mr The memory resource for frame allocation.
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with memory resource and handlers.

    @param ex The executor to execute the task on.
    @param mr The memory resource for frame allocation.
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with stop token and memory resource.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param mr The memory resource for frame allocation.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with stop token, memory resource, and handler.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param mr The memory resource for frame allocation.
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with stop token, memory resource, and handlers.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param mr The memory resource for frame allocation.
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with custom allocator.

    The allocator is wrapped in a frame_memory_resource and stored in the
    run_async_trampoline, ensuring it outlives all coroutine frames.

    @param ex The executor to execute the task on.
    @param alloc The allocator for frame allocation (copied and stored).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with allocator and handler.

    @param ex The executor to execute the task on.
    @param alloc The allocator for frame allocation (copied and stored).
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with allocator and handlers.

    @param ex The executor to execute the task on.
    @param alloc The allocator for frame allocation (copied and stored).
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with stop token and allocator.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param alloc The allocator for frame allocation (copied and stored).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with stop token, allocator, and handler.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param alloc The allocator for frame allocation (copied and stored).
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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

/** Asynchronously launch a lazy task with stop token, allocator, and handlers.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param alloc The allocator for frame allocation (copied and stored).
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
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
