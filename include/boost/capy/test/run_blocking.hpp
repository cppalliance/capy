//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_RUN_BLOCKING_HPP
#define BOOST_CAPY_TEST_RUN_BLOCKING_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/execution_context.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/run_async.hpp>

#include <coroutine>
#include <exception>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace test {

class blocking_context;

/** Single-threaded executor for blocking synchronous tests.

    This executor is used internally by @ref run_blocking to
    execute coroutine tasks on the calling thread. Work submitted
    via `dispatch()` is returned for symmetric transfer. Work
    submitted via `post()` is enqueued and processed by the
    @ref blocking_context event loop.

    Users do not construct this type directly. It is obtained
    from @ref blocking_context::get_executor.

    @par Thread Safety
    All member functions are safe to call from any thread.

    @see blocking_context, run_blocking
*/
struct BOOST_CAPY_DECL blocking_executor
{
    /// Construct from a context pointer.
    explicit blocking_executor(
        blocking_context* ctx) noexcept
        : ctx_(ctx)
    {
    }

    /** Compare two blocking executors for equality.

        Two executors are equal if they share the same context.
    */
    bool
    operator==(blocking_executor const& other) const noexcept;

    /** Return the associated execution context.

        @return A reference to the owning `blocking_context`.
    */
    blocking_context&
    context() const noexcept;

    /// Called when work is submitted (no-op).
    void on_work_started() const noexcept;

    /// Called when work completes (no-op).
    void on_work_finished() const noexcept;

    /** Dispatch work for immediate inline execution.

        Returns the handle for symmetric transfer. The caller
        resumes the coroutine via the returned handle.

        @param h The coroutine handle to execute.

        @return `h` for symmetric transfer.
    */
    std::coroutine_handle<>
    dispatch(std::coroutine_handle<> h) const;

    /** Post work for deferred execution.

        Enqueues the coroutine handle into the context's work
        queue. The handle is resumed when the blocking event
        loop processes it.

        @param h The coroutine handle to enqueue.
    */
    void
    post(std::coroutine_handle<> h) const;

private:
    blocking_context* ctx_;
};

/** Single-threaded execution context for blocking tests.

    Provides a work queue and event loop that runs on the
    calling thread. Coroutines dispatched through the
    associated @ref blocking_executor have their `post()`
    calls enqueued and processed by @ref run, which blocks
    until @ref signal_done is called.

    This context is created internally by @ref run_blocking.
    Users do not interact with it directly.

    @par Thread Safety
    The event loop runs on the thread that calls `run()`.
    `signal_done()` and `enqueue()` are safe to call from
    any thread.

    @see blocking_executor, run_blocking
*/
class BOOST_CAPY_DECL blocking_context
    : public execution_context
{
    struct impl;
    impl* impl_;

public:
    using executor_type = blocking_executor;

    /** Construct a blocking context.

        Allocates the internal work queue and
        synchronization state.
    */
    blocking_context();

    /** Destroy the blocking context. */
    ~blocking_context();

    /** Return an executor bound to this context.

        @return A `blocking_executor` that enqueues work
            into this context's queue.
    */
    blocking_executor
    get_executor() noexcept;

    /** Signal that the task has completed.

        Wakes the event loop so that @ref run returns.
    */
    void
    signal_done() noexcept;

    /** Signal that the task has completed with an error.

        Stores the exception and wakes the event loop
        so that @ref run rethrows it.

        @param ep The exception to propagate.
    */
    void
    signal_done(std::exception_ptr ep) noexcept;

    /** Run the event loop until done.

        Blocks the calling thread, processing posted
        coroutine handles until @ref signal_done is called.
        After draining remaining work, rethrows any stored
        exception.

        @par Exception Safety
        Basic guarantee. If the completed task stored an
        exception via `signal_done(ep)`, it is rethrown.
    */
    void
    run();

    /** Enqueue a coroutine handle for processing.

        @param h The coroutine handle to enqueue.
    */
    void
    enqueue(std::coroutine_handle<> h);
};

/** Wrapper that signals completion after invoking the handler.

    Forwards invocations to the contained handler_pair, then
    signals the `blocking_context` so that its event loop
    unblocks. Exceptions thrown by the handler are captured
    and stored for later rethrow.

    @tparam H1 The success handler type.
    @tparam H2 The error handler type.

    @par Thread Safety
    Safe to invoke from any thread.

    @see run_blocking, blocking_context
*/
template<class H1, class H2>
struct blocking_handler_wrapper
{
    blocking_context* ctx_;
    detail::handler_pair<H1, H2> handlers_;

    /** Invoke the handler with a non-void result. */
    template<class T>
    void operator()(T&& v)
    {
        try
        {
            handlers_(std::forward<T>(v));
        }
        catch(...)
        {
            ctx_->signal_done(std::current_exception());
            return;
        }
        ctx_->signal_done();
    }

    /** Invoke the handler for a void result. */
    void operator()()
    {
        try
        {
            handlers_();
        }
        catch(...)
        {
            ctx_->signal_done(std::current_exception());
            return;
        }
        ctx_->signal_done();
    }

    /** Invoke the handler with an exception. */
    void operator()(std::exception_ptr ep)
    {
        try
        {
            handlers_(ep);
        }
        catch(...)
        {
            ctx_->signal_done(std::current_exception());
            return;
        }
        ctx_->signal_done();
    }
};

/** Wrapper returned by run_blocking that accepts a task.

    Holds the handlers and optional stop token. When invoked
    with a task, creates a @ref blocking_context, launches
    the task via `run_async`, and pumps the event loop until
    the task completes.

    The rvalue ref-qualifier on `operator()` ensures the
    wrapper can only be used as a temporary.

    @tparam H1 The success handler type.
    @tparam H2 The error handler type.

    @par Thread Safety
    The wrapper itself should only be used from one thread.
    The calling thread blocks until the task completes.

    @par Example
    @code
    int result = 0;
    run_blocking([&](int v) { result = v; })(my_task());
    @endcode

    @see run_blocking, run_async
*/
template<class H1, class H2>
class [[nodiscard]] run_blocking_wrapper
{
    std::stop_token st_;
    H1 h1_;
    H2 h2_;

public:
    /** Construct wrapper with stop token and handlers.

        @param st The stop token for cooperative cancellation.
        @param h1 The success handler.
        @param h2 The error handler.
    */
    run_blocking_wrapper(
        std::stop_token st,
        H1 h1,
        H2 h2)
        : st_(std::move(st))
        , h1_(std::move(h1))
        , h2_(std::move(h2))
    {
    }

    run_blocking_wrapper(run_blocking_wrapper const&) = delete;
    run_blocking_wrapper(run_blocking_wrapper&&) = delete;
    run_blocking_wrapper& operator=(run_blocking_wrapper const&) = delete;
    run_blocking_wrapper& operator=(run_blocking_wrapper&&) = delete;

    /** Launch the task and block until completion.

        Creates a blocking_context with a single-threaded
        event loop, launches the task via `run_async`, then
        pumps the loop until the task completes or throws.

        @tparam Task The IoRunnable type.

        @param t The task to execute.
    */
    template<IoRunnable Task>
    void
    operator()(Task t) &&
    {
        blocking_context ctx;

        auto make_handlers = [&]() {
            if constexpr(
                std::is_same_v<H2, detail::default_handler>)
                return detail::handler_pair<H1, H2>{
                    std::move(h1_)};
            else
                return detail::handler_pair<H1, H2>{
                    std::move(h1_), std::move(h2_)};
        };

        run_async(
            ctx.get_executor(),
            st_,
            blocking_handler_wrapper<H1, H2>{
                &ctx, make_handlers()}
        )(std::move(t));

        ctx.run();
    }
};

/** Block until task completes and discard result.

    Executes a lazy task on a single-threaded event loop
    and blocks the calling thread until the task completes
    or throws.

    @par Exception Safety
    Basic guarantee. If the task throws, the exception is
    rethrown to the caller.

    @par Example
    @code
    run_blocking()(my_void_task());
    @endcode

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
[[nodiscard]] inline auto
run_blocking()
{
    return run_blocking_wrapper<
        detail::default_handler,
        detail::default_handler>(
            std::stop_token{},
            detail::default_handler{},
            detail::default_handler{});
}

/** Block until task completes and invoke handler with result.

    Executes a lazy task on a single-threaded event loop
    and blocks until completion. The handler `h1` is called
    with the result on success. If `h1` is also invocable
    with `std::exception_ptr`, it handles exceptions too.
    Otherwise, exceptions are rethrown.

    @par Exception Safety
    Basic guarantee. Exceptions from the task are passed
    to `h1` if it accepts `std::exception_ptr`, otherwise
    rethrown.

    @par Example
    @code
    int result = 0;
    run_blocking([&](int v) { result = v; })(compute());
    @endcode

    @param h1 Handler invoked with the result on success,
        and optionally with `std::exception_ptr` on failure.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
template<class H1>
[[nodiscard]] auto
run_blocking(H1 h1)
{
    return run_blocking_wrapper<
        H1,
        detail::default_handler>(
            std::stop_token{},
            std::move(h1),
            detail::default_handler{});
}

/** Block until task completes with separate handlers.

    Executes a lazy task on a single-threaded event loop
    and blocks until completion. The handler `h1` is called
    on success, `h2` on failure.

    @par Exception Safety
    Basic guarantee. Exceptions from the task are passed
    to `h2`.

    @par Example
    @code
    int result = 0;
    run_blocking(
        [&](int v) { result = v; },
        [](std::exception_ptr ep) {
            std::rethrow_exception(ep);
        }
    )(compute());
    @endcode

    @param h1 Handler invoked with the result on success.
    @param h2 Handler invoked with the exception on failure.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
template<class H1, class H2>
[[nodiscard]] auto
run_blocking(H1 h1, H2 h2)
{
    return run_blocking_wrapper<
        H1,
        H2>(
            std::stop_token{},
            std::move(h1),
            std::move(h2));
}

/** Block until task completes with stop token support.

    Executes a lazy task on a single-threaded event loop
    with the given stop token and blocks until completion.

    @par Exception Safety
    Basic guarantee. If the task throws, the exception is
    rethrown to the caller.

    @param st The stop token for cooperative cancellation.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
[[nodiscard]] inline auto
run_blocking(std::stop_token st)
{
    return run_blocking_wrapper<
        detail::default_handler,
        detail::default_handler>(
            std::move(st),
            detail::default_handler{},
            detail::default_handler{});
}

/** Block until task completes with stop token and handler.

    @param st The stop token for cooperative cancellation.
    @param h1 Handler invoked with the result on success.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
template<class H1>
[[nodiscard]] auto
run_blocking(std::stop_token st, H1 h1)
{
    return run_blocking_wrapper<
        H1,
        detail::default_handler>(
            std::move(st),
            std::move(h1),
            detail::default_handler{});
}

/** Block until task completes with stop token and handlers.

    @param st The stop token for cooperative cancellation.
    @param h1 Handler invoked with the result on success.
    @param h2 Handler invoked with the exception on failure.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
template<class H1, class H2>
[[nodiscard]] auto
run_blocking(std::stop_token st, H1 h1, H2 h2)
{
    return run_blocking_wrapper<
        H1,
        H2>(
            std::move(st),
            std::move(h1),
            std::move(h2));
}

} // namespace test
} // namespace capy
} // namespace boost

#endif
