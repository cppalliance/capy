//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_RUN_ASYNC_HPP
#define BOOST_CAPY_RUN_ASYNC_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/frame_allocator.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/ex/stop_token.hpp>

#include <concepts>
#include <coroutine>
#include <exception>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

//----------------------------------------------------------
//
// Handler Types
//
//----------------------------------------------------------

/** Default handler for run_async that discards results and rethrows exceptions.

    This handler type is used when no user-provided handlers are specified.
    On successful completion it discards the result value. On exception it
    rethrows the exception from the exception_ptr.

    @par Thread Safety
    All member functions are thread-safe.

    @see run_async
    @see handler_pair
*/
struct default_handler
{
    /// Discard a non-void result value.
    template<class T>
    void operator()(T&&) const noexcept
    {
    }

    /// Handle void result (no-op).
    void operator()() const noexcept
    {
    }

    /// Rethrow the captured exception.
    void operator()(std::exception_ptr ep) const
    {
        if(ep)
            std::rethrow_exception(ep);
    }
};

/** Combines two handlers into one: h1 for success, h2 for exception.

    This class template wraps a success handler and an error handler,
    providing a unified callable interface for the trampoline coroutine.

    @tparam H1 The success handler type. Must be invocable with `T&&` for
               non-void tasks or with no arguments for void tasks.
    @tparam H2 The error handler type. Must be invocable with `std::exception_ptr`.

    @par Thread Safety
    Thread safety depends on the contained handlers.

    @see run_async
    @see default_handler
*/
template<class H1, class H2>
struct handler_pair
{
    H1 h1_;
    H2 h2_;

    /// Invoke success handler with non-void result.
    template<class T>
    void operator()(T&& v)
    {
        h1_(std::forward<T>(v));
    }

    /// Invoke success handler for void result.
    void operator()()
    {
        h1_();
    }

    /// Invoke error handler with exception.
    void operator()(std::exception_ptr ep)
    {
        h2_(ep);
    }
};

/** Specialization for single handler that may handle both success and error.

    When only one handler is provided to `run_async`, this specialization
    checks at compile time whether the handler can accept `std::exception_ptr`.
    If so, it routes exceptions to the handler. Otherwise, exceptions are
    rethrown (the default behavior).

    @tparam H1 The handler type. If invocable with `std::exception_ptr`,
               it handles both success and error cases.

    @par Thread Safety
    Thread safety depends on the contained handler.

    @see run_async
    @see default_handler
*/
template<class H1>
struct handler_pair<H1, default_handler>
{
    H1 h1_;

    /// Invoke handler with non-void result.
    template<class T>
    void operator()(T&& v)
    {
        h1_(std::forward<T>(v));
    }

    /// Invoke handler for void result.
    void operator()()
    {
        h1_();
    }

    /// Route exception to h1 if it accepts exception_ptr, otherwise rethrow.
    void operator()(std::exception_ptr ep)
    {
        if constexpr(std::invocable<H1, std::exception_ptr>)
            h1_(ep);
        else
            std::rethrow_exception(ep);
    }
};

namespace detail {

//----------------------------------------------------------
//
// Trampoline Coroutine
//
//----------------------------------------------------------

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

/** Internal trampoline coroutine for run_async.

    The trampoline is allocated BEFORE the task (via C++17 postfix evaluation
    order) and serves as the task's continuation. When the task final_suspends,
    control returns to the trampoline which then invokes the appropriate handler.

    @tparam Ex The executor type.
    @tparam Handlers The handler type (default_handler or handler_pair).
*/
template<class Ex, class Handlers>
struct trampoline
{
    using invoke_fn = void(*)(void*, Handlers&);

    struct promise_type
    {
        Ex ex_;
        Handlers handlers_;
        invoke_fn invoke_ = nullptr;
        void* task_promise_ = nullptr;
        std::coroutine_handle<> task_h_;

        // Constructor receives coroutine parameters by lvalue reference
        promise_type(Ex ex, Handlers h)
            : ex_(std::move(ex))
            , handlers_(std::move(h))
        {
        }

        trampoline get_return_object() noexcept
        {
            return trampoline{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        // Self-destruct after invoking handlers
        std::suspend_never final_suspend() noexcept
        {
            return {};
        }

        void return_void() noexcept
        {
        }

        void unhandled_exception() noexcept
        {
            // Handler threw - this is undefined behavior if no error handler provided
        }
    };

    std::coroutine_handle<promise_type> h_;

    /// Type-erased invoke function instantiated per task<T>.
    template<class T>
    static void invoke_impl(void* p, Handlers& h)
    {
        auto& promise = *static_cast<typename task<T>::promise_type*>(p);
        if(promise.ep_)
            h(promise.ep_);
        else if constexpr(std::is_void_v<T>)
            h();
        else
            h(std::move(*promise.result_));
    }
};

/// Coroutine body for trampoline - invokes handlers then destroys task.
template<class Ex, class Handlers>
trampoline<Ex, Handlers>
make_trampoline(Ex ex, Handlers h)
{
    // Parameters are passed to promise_type constructor by coroutine machinery
    (void)ex;
    (void)h;
    auto& p = co_await get_promise_awaiter<typename trampoline<Ex, Handlers>::promise_type>{};
    
    // Invoke the type-erased handler
    p.invoke_(p.task_promise_, p.handlers_);
    
    // Destroy task (LIFO: task destroyed first, trampoline destroyed after)
    p.task_h_.destroy();
}

} // namespace detail

//----------------------------------------------------------
//
// run_async_wrapper
//
//----------------------------------------------------------

/** Wrapper returned by run_async that accepts a task for execution.

    This wrapper holds the trampoline coroutine, executor, stop token,
    and handlers. The trampoline is allocated when the wrapper is constructed
    (before the task due to C++17 postfix evaluation order).

    The rvalue ref-qualifier on `operator()` ensures the wrapper can only
    be used as a temporary, preventing misuse that would violate LIFO ordering.

    @tparam Ex The executor type satisfying the `Executor` concept.
    @tparam Handlers The handler type (default_handler or handler_pair).

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
template<Executor Ex, class Handlers>
class [[nodiscard]] run_async_wrapper
{
    detail::trampoline<Ex, Handlers> tr_;
    capy::stop_token st_;

public:
    /// Construct wrapper with executor, stop token, and handlers.
    run_async_wrapper(
        Ex ex,
        capy::stop_token st,
        Handlers h)
        : tr_(detail::make_trampoline<Ex, Handlers>(
            std::move(ex), std::move(h)))
        , st_(std::move(st))
    {
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

        @tparam T The task's return type.

        @param t The task to execute. Ownership is transferred to the
                 trampoline which will destroy it after completion.
    */
    template<class T>
    void operator()(task<T> t) &&
    {
        auto task_h = t.release();
        auto& p = tr_.h_.promise();

        // Inject T-specific invoke function
        p.invoke_ = detail::trampoline<Ex, Handlers>::template invoke_impl<T>;
        p.task_promise_ = &task_h.promise();
        p.task_h_ = task_h;

        // Setup task's continuation to return to trampoline
        // Executor lives in trampoline's promise, so reference is valid for task's lifetime
        task_h.promise().continuation_ = tr_.h_;
        task_h.promise().caller_ex_ = p.ex_;
        task_h.promise().set_executor(p.ex_);
        task_h.promise().set_stop_token(st_);

        // Resume task through executor
        // The executor returns a handle for symmetric transfer;
        // from non-coroutine code we must explicitly resume it
        p.ex_.dispatch(task_h).resume();
    }
};

//----------------------------------------------------------
//
// run_async Overloads
//
//----------------------------------------------------------

// Executor only

/** Asynchronously launch a lazy task on the given executor.

    Use this to start execution of a `task<T>` that was created lazily.
    The returned wrapper must be immediately invoked with the task;
    storing the wrapper and calling it later violates LIFO ordering.

    With no handlers, the result is discarded and exceptions are rethrown.

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
    return run_async_wrapper<Ex, default_handler>(
        std::move(ex),
        capy::stop_token{},
        default_handler{});
}

/** Asynchronously launch a lazy task with a result handler.

    The handler `h1` is called with the task's result on success. If `h1`
    is also invocable with `std::exception_ptr`, it handles exceptions too.
    Otherwise, exceptions are rethrown.

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
    return run_async_wrapper<Ex, handler_pair<H1, default_handler>>(
        std::move(ex),
        capy::stop_token{},
        handler_pair<H1, default_handler>{std::move(h1)});
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
    return run_async_wrapper<Ex, handler_pair<H1, H2>>(
        std::move(ex),
        capy::stop_token{},
        handler_pair<H1, H2>{std::move(h1), std::move(h2)});
}

// Ex + stop_token

/** Asynchronously launch a lazy task with stop token support.

    The stop token is propagated to the task, enabling cooperative
    cancellation. With no handlers, the result is discarded and
    exceptions are rethrown.

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
    run_async(Ex ex, capy::stop_token st)
    {
        return run_async_wrapper<Ex, default_handler>(
            std::move(ex),
            std::move(st),
            default_handler{});
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
run_async(Ex ex, stop_token st, H1 h1)
{
    return run_async_wrapper<Ex, handler_pair<H1, default_handler>>(
        std::move(ex),
        std::move(st),
        handler_pair<H1, default_handler>{std::move(h1)});
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
    run_async(Ex ex, capy::stop_token st, H1 h1, H2 h2)
{
    return run_async_wrapper<Ex, handler_pair<H1, H2>>(
        std::move(ex),
        std::move(st),
        handler_pair<H1, H2>{std::move(h1), std::move(h2)});
}

// Executor + stop_token + allocator

/** Asynchronously launch a lazy task with stop token and allocator.

    The stop token is propagated to the task for cooperative cancellation.
    The allocator parameter is reserved for future use and currently ignored.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param alloc The frame allocator (currently ignored).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
    @see frame_allocator
*/
    template<Executor Ex, FrameAllocator FA>
    [[nodiscard]] auto
    run_async(Ex ex, capy::stop_token st, FA alloc)
{
    (void)alloc; // Currently ignored
    return run_async_wrapper<Ex, default_handler>(
        std::move(ex),
        std::move(st),
        default_handler{});
}

/** Asynchronously launch a lazy task with stop token, allocator, and handler.

    The stop token is propagated to the task for cooperative cancellation.
    The allocator parameter is reserved for future use and currently ignored.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param alloc The frame allocator (currently ignored).
    @param h1 The handler to invoke with the result (and optionally exception).

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
    @see frame_allocator
*/
    template<Executor Ex, FrameAllocator FA, class H1>
    [[nodiscard]] auto
    run_async(Ex ex, capy::stop_token st, FA alloc, H1 h1)
{
    (void)alloc; // Currently ignored
    return run_async_wrapper<Ex, handler_pair<H1, default_handler>>(
        std::move(ex),
        std::move(st),
        handler_pair<H1, default_handler>{std::move(h1)});
}

/** Asynchronously launch a lazy task with stop token, allocator, and handlers.

    The stop token is propagated to the task for cooperative cancellation.
    The allocator parameter is reserved for future use and currently ignored.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param alloc The frame allocator (currently ignored).
    @param h1 The handler to invoke with the result on success.
    @param h2 The handler to invoke with the exception on failure.

    @return A wrapper that accepts a `task<T>` for immediate execution.

    @see task
    @see executor
    @see frame_allocator
*/
    template<Executor Ex, FrameAllocator FA, class H1, class H2>
    [[nodiscard]] auto
    run_async(Ex ex, capy::stop_token st, FA alloc, H1 h1, H2 h2)
{
    (void)alloc; // Currently ignored
    return run_async_wrapper<Ex, handler_pair<H1, H2>>(
        std::move(ex),
        std::move(st),
        handler_pair<H1, H2>{std::move(h1), std::move(h2)});
}

} // namespace capy
} // namespace boost

#endif
