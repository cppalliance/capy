//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_RUN_BLOCKING_HPP
#define BOOST_CAPY_TEST_RUN_BLOCKING_HPP

#include <boost/capy/coro.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/run_async.hpp>

#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace test {

/** Synchronous executor that executes inline and disallows posting.

    This executor executes work synchronously on the calling thread
    via `dispatch()`. Calling `post()` throws `std::logic_error`
    because posting implies deferred execution which is incompatible
    with blocking synchronous semantics.

    @par Thread Safety
    All member functions are thread-safe.

    @see run_blocking
*/
struct inline_executor
{
    /// Compare two inline executors for equality.
    bool
    operator==(inline_executor const&) const noexcept
    {
        return true;
    }

    /** Return the associated execution context.

        @return A reference to a function-local static `inline_context`.
    */
    execution_context&
    context() const noexcept
    {
        struct inline_context : public execution_context {};
        static inline_context ctx;
        return ctx;
    }

    /// Called when work is submitted (no-op).
    void on_work_started() const noexcept {}

    /// Called when work completes (no-op).
    void on_work_finished() const noexcept {}

    /** Dispatch work for immediate inline execution.

        @param h The coroutine handle to execute.

        @return The same handle for symmetric transfer.
    */
    coro
    dispatch(coro h) const
    {
        return h;
    }

    /** Post work for deferred execution.

        @par Exception Safety
        Always throws.

        @throws std::logic_error Always, because posting is not
            supported in a blocking context.

        @param h The coroutine handle (unused).
    */
    [[noreturn]] void
    post(coro) const
    {
        throw std::logic_error(
            "post not supported in blocking context");
    }
};

static_assert(Executor<inline_executor>);

//----------------------------------------------------------
//
// blocking_state
//
//----------------------------------------------------------

/** Synchronization state for blocking execution.

    Holds the mutex, condition variable, and completion flag
    used to block the caller until the task completes.

    @par Thread Safety
    Thread-safe when accessed under the mutex.

    @see run_blocking
    @see blocking_handler_wrapper
*/
struct blocking_state
{
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    std::exception_ptr ep;
};

//----------------------------------------------------------
//
// blocking_handler_wrapper
//
//----------------------------------------------------------

/** Wrapper that signals completion after invoking the underlying handler_pair.

    This wrapper forwards invocations to the contained handler_pair,
    then signals the `blocking_state` condition variable so
    that `run_blocking` can unblock. Any exceptions thrown by the
    handler are captured and stored for later rethrow.

    @tparam H1 The success handler type.
    @tparam H2 The error handler type.

    @par Thread Safety
    Safe to invoke from any thread. Signals the condition
    variable after calling the handler.

    @see run_blocking
    @see blocking_state
*/
template<class H1, class H2>
struct blocking_handler_wrapper
{
    blocking_state* state_;
    detail::handler_pair<H1, H2> handlers_;

    /** Invoke the handler with non-void result and signal completion. */
    template<class T>
    void operator()(T&& v)
    {
        try
        {
            handlers_(std::forward<T>(v));
        }
        catch(...)
        {
            std::lock_guard<std::mutex> lock(state_->mtx);
            state_->ep = std::current_exception();
            state_->done = true;
            state_->cv.notify_one();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(state_->mtx);
            state_->done = true;
        }
        state_->cv.notify_one();
    }

    /** Invoke the handler for void result and signal completion. */
    void operator()()
    {
        try
        {
            handlers_();
        }
        catch(...)
        {
            std::lock_guard<std::mutex> lock(state_->mtx);
            state_->ep = std::current_exception();
            state_->done = true;
            state_->cv.notify_one();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(state_->mtx);
            state_->done = true;
        }
        state_->cv.notify_one();
    }

    /** Invoke the handler with exception and signal completion. */
    void operator()(std::exception_ptr ep)
    {
        try
        {
            handlers_(ep);
        }
        catch(...)
        {
            std::lock_guard<std::mutex> lock(state_->mtx);
            state_->ep = std::current_exception();
            state_->done = true;
            state_->cv.notify_one();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(state_->mtx);
            state_->done = true;
        }
        state_->cv.notify_one();
    }
};

//----------------------------------------------------------
//
// run_blocking_wrapper
//
//----------------------------------------------------------

/** Wrapper returned by run_blocking that accepts a task for execution.

    This wrapper holds the blocking state and handlers. When invoked
    with a task, it launches the task via `run_async` and blocks
    until the task completes.

    The rvalue ref-qualifier on `operator()` ensures the wrapper
    can only be used as a temporary.

    @tparam Ex The executor type satisfying the `Executor` concept.
    @tparam H1 The success handler type.
    @tparam H2 The error handler type.

    @par Thread Safety
    The wrapper itself should only be used from one thread.
    The calling thread blocks until the task completes.

    @par Example
    @code
    // Block until task completes
    int result = 0;
    run_blocking([&](int v) { result = v; })(my_task());
    @endcode

    @see run_blocking
    @see run_async
*/
template<Executor Ex, class H1, class H2>
class [[nodiscard]] run_blocking_wrapper
{
    Ex ex_;
    std::stop_token st_;
    H1 h1_;
    H2 h2_;

public:
    /** Construct wrapper with executor, stop token, and handlers.

        @param ex The executor to execute the task on.
        @param st The stop token for cooperative cancellation.
        @param h1 The success handler.
        @param h2 The error handler.
    */
    run_blocking_wrapper(
        Ex ex,
        std::stop_token st,
        H1 h1,
        H2 h2)
        : ex_(std::move(ex))
        , st_(std::move(st))
        , h1_(std::move(h1))
        , h2_(std::move(h2))
    {
    }

    run_blocking_wrapper(run_blocking_wrapper const&) = delete;
    run_blocking_wrapper(run_blocking_wrapper&&) = delete;
    run_blocking_wrapper& operator=(run_blocking_wrapper const&) = delete;
    run_blocking_wrapper& operator=(run_blocking_wrapper&&) = delete;

    /** Launch the task and block until completion.

        This operator accepts a task, launches it via `run_async`
        with wrapped handlers, and blocks until the task completes.

        @tparam Task The IoLaunchableTask type.

        @param t The task to execute.
    */
    template<IoLaunchableTask Task>
    void
    operator()(Task t) &&
    {
        blocking_state state;

        auto make_handlers = [&]() {
            if constexpr(std::is_same_v<H2, detail::default_handler>)
                return detail::handler_pair<H1, H2>{std::move(h1_)};
            else
                return detail::handler_pair<H1, H2>{std::move(h1_), std::move(h2_)};
        };

        run_async(
            ex_,
            st_,
            blocking_handler_wrapper<H1, H2>{&state, make_handlers()}
        )(std::move(t));

        std::unique_lock<std::mutex> lock(state.mtx);
        state.cv.wait(lock, [&] { return state.done; });
        if(state.ep)
            std::rethrow_exception(state.ep);
    }
};

//----------------------------------------------------------
//
// run_blocking Overloads
//
//----------------------------------------------------------

// With inline_executor (default)

/** Block until task completes and discard result.

    Executes a lazy task using the inline executor and blocks the
    calling thread until the task completes or throws.

    @par Thread Safety
    The calling thread is blocked. The task executes inline
    on the calling thread.

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
        inline_executor,
        detail::default_handler,
        detail::default_handler>(
            inline_executor{},
            std::stop_token{},
            detail::default_handler{},
            detail::default_handler{});
}

/** Block until task completes and invoke handler with result.

    Executes a lazy task using the inline executor and blocks until
    completion. The handler `h1` is called with the result on success.
    If `h1` is also invocable with `std::exception_ptr`, it handles
    exceptions too. Otherwise, exceptions are rethrown.

    @par Thread Safety
    The calling thread is blocked. The task and handler execute
    inline on the calling thread.

    @par Exception Safety
    Basic guarantee. Exceptions from the task are passed to `h1`
    if it accepts `std::exception_ptr`, otherwise rethrown.

    @par Example
    @code
    int result = 0;
    run_blocking([&](int v) { result = v; })(compute_value());
    @endcode

    @param h1 Handler invoked with the result on success, and
        optionally with `std::exception_ptr` on failure.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
template<class H1>
[[nodiscard]] auto
run_blocking(H1 h1)
{
    return run_blocking_wrapper<
        inline_executor,
        H1,
        detail::default_handler>(
            inline_executor{},
            std::stop_token{},
            std::move(h1),
            detail::default_handler{});
}

/** Block until task completes with separate handlers.

    Executes a lazy task using the inline executor and blocks until
    completion. The handler `h1` is called on success, `h2` on failure.

    @par Thread Safety
    The calling thread is blocked. The task and handlers execute
    inline on the calling thread.

    @par Exception Safety
    Basic guarantee. Exceptions from the task are passed to `h2`.

    @par Example
    @code
    int result = 0;
    run_blocking(
        [&](int v) { result = v; },
        [](std::exception_ptr ep) {
            std::rethrow_exception(ep);
        }
    )(compute_value());
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
        inline_executor,
        H1,
        H2>(
            inline_executor{},
            std::stop_token{},
            std::move(h1),
            std::move(h2));
}

// With explicit executor

/** Block until task completes on the given executor.

    Executes a lazy task on the specified executor and blocks the
    calling thread until the task completes.

    @par Thread Safety
    The calling thread is blocked. The task may execute on
    a different thread depending on the executor.

    @par Exception Safety
    Basic guarantee. If the task throws, the exception is
    rethrown to the caller.

    @par Example
    @code
    run_blocking(my_executor)(my_void_task());
    @endcode

    @param ex The executor to execute the task on.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
template<Executor Ex>
[[nodiscard]] auto
run_blocking(Ex ex)
{
    return run_blocking_wrapper<
        Ex,
        detail::default_handler,
        detail::default_handler>(
            std::move(ex),
            std::stop_token{},
            detail::default_handler{},
            detail::default_handler{});
}

/** Block until task completes on executor with handler.

    Executes a lazy task on the specified executor and blocks until
    completion. The handler `h1` is called with the result.

    @par Thread Safety
    The calling thread is blocked. The task and handler may
    execute on a different thread depending on the executor.

    @par Exception Safety
    Basic guarantee. Exceptions from the task are passed to `h1`
    if it accepts `std::exception_ptr`, otherwise rethrown.

    @param ex The executor to execute the task on.
    @param h1 Handler invoked with the result on success.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
template<Executor Ex, class H1>
[[nodiscard]] auto
run_blocking(Ex ex, H1 h1)
{
    return run_blocking_wrapper<
        Ex,
        H1,
        detail::default_handler>(
            std::move(ex),
            std::stop_token{},
            std::move(h1),
            detail::default_handler{});
}

/** Block until task completes on executor with separate handlers.

    Executes a lazy task on the specified executor and blocks until
    completion. The handler `h1` is called on success, `h2` on failure.

    @par Thread Safety
    The calling thread is blocked. The task and handlers may
    execute on a different thread depending on the executor.

    @par Exception Safety
    Basic guarantee. Exceptions from the task are passed to `h2`.

    @param ex The executor to execute the task on.
    @param h1 Handler invoked with the result on success.
    @param h2 Handler invoked with the exception on failure.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
template<Executor Ex, class H1, class H2>
[[nodiscard]] auto
run_blocking(Ex ex, H1 h1, H2 h2)
{
    return run_blocking_wrapper<
        Ex,
        H1,
        H2>(
            std::move(ex),
            std::stop_token{},
            std::move(h1),
            std::move(h2));
}

// With stop_token

/** Block until task completes with stop token support.

    Executes a lazy task using the inline executor with the given
    stop token and blocks until completion.

    @par Thread Safety
    The calling thread is blocked. The task executes inline
    on the calling thread.

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
        inline_executor,
        detail::default_handler,
        detail::default_handler>(
            inline_executor{},
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
        inline_executor,
        H1,
        detail::default_handler>(
            inline_executor{},
            std::move(st),
            std::move(h1),
            detail::default_handler{});
}

/** Block until task completes with stop token and separate handlers.

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
        inline_executor,
        H1,
        H2>(
            inline_executor{},
            std::move(st),
            std::move(h1),
            std::move(h2));
}

// Executor + stop_token

/** Block until task completes on executor with stop token.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
template<Executor Ex>
[[nodiscard]] auto
run_blocking(Ex ex, std::stop_token st)
{
    return run_blocking_wrapper<
        Ex,
        detail::default_handler,
        detail::default_handler>(
            std::move(ex),
            std::move(st),
            detail::default_handler{},
            detail::default_handler{});
}

/** Block until task completes on executor with stop token and handler.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param h1 Handler invoked with the result on success.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
template<Executor Ex, class H1>
[[nodiscard]] auto
run_blocking(Ex ex, std::stop_token st, H1 h1)
{
    return run_blocking_wrapper<
        Ex,
        H1,
        detail::default_handler>(
            std::move(ex),
            std::move(st),
            std::move(h1),
            detail::default_handler{});
}

/** Block until task completes on executor with stop token and handlers.

    @param ex The executor to execute the task on.
    @param st The stop token for cooperative cancellation.
    @param h1 Handler invoked with the result on success.
    @param h2 Handler invoked with the exception on failure.

    @return A wrapper that accepts a task for blocking execution.

    @see run_async
*/
template<Executor Ex, class H1, class H2>
[[nodiscard]] auto
run_blocking(Ex ex, std::stop_token st, H1 h1, H2 h2)
{
    return run_blocking_wrapper<
        Ex,
        H1,
        H2>(
            std::move(ex),
            std::move(st),
            std::move(h1),
            std::move(h2));
}

} // namespace test
} // namespace capy
} // namespace boost

#endif
