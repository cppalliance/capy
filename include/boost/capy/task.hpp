// Copyright Vinnie Falco
// SPDX-License-Identifier: BSL-1.0

/**
    @file task.hpp

    Lazy coroutine task type with executor affinity.

    Provides task<T>, a lazy coroutine that produces a value of type T,
    and spawn() for running tasks with completion handlers. Tasks support
    executor affinity via on() to control which executor resumes the
    coroutine after each co_await.
*/

#ifndef BOOST_CAPY_TASK_HPP
#define BOOST_CAPY_TASK_HPP

#include <boost/capy/detail/config.hpp>

#ifdef BOOST_CAPY_HAS_CORO

#include <boost/capy/affine.hpp>
#include <boost/capy/async_op.hpp>
#include <boost/capy/executor.hpp>

#include <boost/system/result.hpp>

#include <coroutine>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

namespace detail {

/** Adapter that wraps executor and satisfies the dispatcher concept.

    This struct provides operator() by delegating to executor::post(),
    enabling use with the affine awaitable protocol. It is stored as
    a data member in the promise to ensure stable lifetime.
*/
struct executor_dispatcher
{
    executor ex_;

    executor_dispatcher() = default;

    explicit
    executor_dispatcher(executor ex) noexcept
        : ex_(std::move(ex))
    {
    }

    template<class F>
    void
    operator()(F&& f) const
    {
        if (ex_)
            ex_.post(std::forward<F>(f));
        else
            std::forward<F>(f)();
    }

    explicit
    operator bool() const noexcept
    {
        return static_cast<bool>(ex_);
    }
};

} // detail

/** A lazy coroutine task that produces a value of type T.

    This class template represents an owning handle to a suspended
    coroutine that will eventually produce a value of type @ref T.
    The coroutine is lazy: it does not begin execution until it is
    awaited or manually resumed via its handle.

    @par Thread Safety
    Distinct objects may be accessed concurrently. Shared objects
    require external synchronization.

    @par Example
    @code
    task<int> compute_value()
    {
        co_return 42;
    }

    task<void> example()
    {
        int result = co_await compute_value();
    }
    @endcode

    @tparam T The type of value produced by the coroutine.

    @see async_op, launch
*/
template<class T>
class task
    : public affine_task<T, task<T>, detail::executor_dispatcher>
{
public:
    /** The coroutine promise type.

        This nested type satisfies the coroutine promise requirements
        and manages the coroutine's result storage and completion
        notification.
    */
    struct promise_type
        : affine_promise<promise_type, detail::executor_dispatcher>
    {
        /// Storage for the result value or exception (empty exception_ptr = incomplete)
        system::result<T, std::exception_ptr> result_{std::exception_ptr{}};

        /// Dispatcher for await_transform (always present for consistent types)
        detail::executor_dispatcher await_dispatcher_{};

        /** Get the executor for affinity.

            @return The executor used for resumption affinity.
        */
        executor
        get_executor() const noexcept
        {
            return await_dispatcher_.ex_;
        }

        /** Set the executor for affinity.

            @param ex The executor to resume on after co_await.
        */
        void
        set_executor(executor ex) noexcept
        {
            await_dispatcher_ = detail::executor_dispatcher{std::move(ex)};
            // Also set on base class for final_suspend behavior
            if (await_dispatcher_)
                this->affine_promise<promise_type, detail::executor_dispatcher>::set_dispatcher(await_dispatcher_);
            else
                this->dispatcher_.reset();
        }

        /** Set the dispatcher for affinity (inheritance).

            Called by affine_task::await_suspend when a parent task
            awaits this task with a dispatcher. Only sets the dispatcher
            if not already set, so explicit affinity via on() takes
            precedence over inherited affinity.

            @param d The dispatcher to use for resumption.
        */
        void
        set_dispatcher(detail::executor_dispatcher d)
        {
            // Only inherit if not explicitly set (explicit affinity takes precedence)
            if (!await_dispatcher_)
            {
                await_dispatcher_ = d;
                this->affine_promise<promise_type, detail::executor_dispatcher>::set_dispatcher(std::move(d));
            }
        }

        /** Transform awaitables for executor affinity.

            Wraps co_await expressions to ensure the coroutine resumes
            on the configured executor. Uses affine_awaiter for
            affine-aware awaitables (zero overhead) and make_affine
            trampoline for legacy awaitables.

            @param a The awaitable to transform.
            @return An affinity-wrapped awaitable.
        */
        template<typename Awaitable>
        auto
        await_transform(Awaitable&& a)
        {
            // Use if constexpr to get consistent return type per branch
            if constexpr (affine_awaitable<Awaitable, detail::executor_dispatcher>)
            {
                // Affine-aware: use affine_awaiter (zero overhead)
                return affine_awaiter{std::forward<Awaitable>(a), &await_dispatcher_};
            }
            else
            {
                // Legacy: use make_affine trampoline
                return make_affine(std::forward<Awaitable>(a), await_dispatcher_);
            }
        }

        /** Returns the task object for this coroutine.

            @return A task owning the coroutine handle.
        */
        task
        get_return_object()
        {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        /** Suspend the coroutine at the start.

            The coroutine is lazy and does not run until awaited.

            @return An awaitable that always suspends.
        */
        std::suspend_always
        initial_suspend() noexcept
        {
            return {};
        }

        /** Store the return value.

            @param v The value to store as the coroutine result.
        */
        void
        return_value(T v)
        {
            result_ = std::move(v);
        }

        /** Store an unhandled exception.

            Captures the current exception for later rethrowing.
        */
        void
        unhandled_exception()
        {
#if defined(__GNUC__) && __GNUC__ >= 12 && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
            result_ = std::current_exception();
#if defined(__GNUC__) && __GNUC__ >= 12 && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
        }

        /** Retrieve the result for await_resume.

            @return The value produced by the coroutine.

            @throws Any exception that was thrown inside the coroutine.
        */
        T
        result()
        {
            if (result_.has_error())
                std::rethrow_exception(result_.error());
            return std::move(*result_);
        }
    };

private:
    std::coroutine_handle<promise_type> h_;

public:
    /** Construct a task from a coroutine handle.

        @param h The coroutine handle to take ownership of.
    */
    explicit
    task(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }

    /** Destructor.

        Destroys the owned coroutine if present.
    */
    ~task()
    {
        if (h_)
            h_.destroy();
    }

    /** Move constructor.

        @param o The task to move from. After the move, @p o will
                 be empty.
    */
    task(task&& o) noexcept
        : h_(std::exchange(o.h_, {}))
    {
    }

    /// Move assignment is deleted.
    task&
    operator=(task&&) = delete;

    /** Access the underlying coroutine handle.

        @return The coroutine handle, without transferring ownership.
    */
    [[nodiscard]]
    std::coroutine_handle<promise_type>
    handle() const noexcept
    {
        return h_;
    }

    /** Release ownership of the coroutine handle.

        After calling this function, the task no longer owns the
        coroutine and the caller becomes responsible for destroying it.

        @return The coroutine handle.
    */
    [[nodiscard]]
    std::coroutine_handle<promise_type>
    release() noexcept
    {
        return std::exchange(h_, {});
    }

    /** Bind this task to an executor for affinity.

        Sets the executor so that when this task's internal
        co_await expressions complete, the coroutine resumes
        on the specified executor. Child tasks without explicit
        affinity will inherit this executor.

        @param e The executor to resume on.

        @return A reference to this task for chaining.

        @par Example
        @code
        task<void> example(executor ex)
        {
            // parse_request resumes on ex after internal co_awaits
            auto data = co_await parse_request().on(ex);
        }
        @endcode
    */
    task&
    on(executor ex) &
    {
        h_.promise().set_executor(std::move(ex));
        return *this;
    }

    /// @copydoc on(executor)
    task&&
    on(executor ex) &&
    {
        h_.promise().set_executor(std::move(ex));
        return std::move(*this);
    }
};

//-----------------------------------------------------------------------------

/** A lazy coroutine task that produces no value.

    This specialization of task is used for coroutines that perform
    work but do not return a value. It uses `co_return;` with no
    argument to complete.

    @par Thread Safety
    Distinct objects may be accessed concurrently. Shared objects
    require external synchronization.

    @par Example
    @code
    task<void> log_message(std::string msg)
    {
        std::cout << msg << std::endl;
        co_return;
    }

    task<void> example()
    {
        co_await log_message("Hello, World!");
    }
    @endcode

    @see task, async_op, launch
*/
template<>
class task<void>
    : public affine_task<void, task<void>, detail::executor_dispatcher>
{
public:
    /** The coroutine promise type for void tasks.

        This nested type satisfies the coroutine promise requirements
        and manages exception storage and completion notification.
    */
    struct promise_type
        : affine_promise<promise_type, detail::executor_dispatcher>
    {
        /// Storage for exception (nullptr = success)
        std::exception_ptr error_;

        /// Dispatcher for await_transform (always present for consistent types)
        detail::executor_dispatcher await_dispatcher_{};

        /** Get the executor for affinity.

            @return The executor used for resumption affinity.
        */
        executor
        get_executor() const noexcept
        {
            return await_dispatcher_.ex_;
        }

        /** Set the executor for affinity.

            @param ex The executor to resume on after co_await.
        */
        void
        set_executor(executor ex) noexcept
        {
            await_dispatcher_ = detail::executor_dispatcher{std::move(ex)};
            // Also set on base class for final_suspend behavior
            if (await_dispatcher_)
                this->affine_promise<promise_type, detail::executor_dispatcher>::set_dispatcher(await_dispatcher_);
            else
                this->dispatcher_.reset();
        }

        /** Set the dispatcher for affinity (inheritance).

            Called by affine_task::await_suspend when a parent task
            awaits this task with a dispatcher. Only sets the dispatcher
            if not already set, so explicit affinity via on() takes
            precedence over inherited affinity.

            @param d The dispatcher to use for resumption.
        */
        void
        set_dispatcher(detail::executor_dispatcher d)
        {
            // Only inherit if not explicitly set (explicit affinity takes precedence)
            if (!await_dispatcher_)
            {
                await_dispatcher_ = d;
                this->affine_promise<promise_type, detail::executor_dispatcher>::set_dispatcher(std::move(d));
            }
        }

        /** Transform awaitables for executor affinity.

            Wraps co_await expressions to ensure the coroutine resumes
            on the configured executor. Uses affine_awaiter for
            affine-aware awaitables (zero overhead) and make_affine
            trampoline for legacy awaitables.

            @param a The awaitable to transform.
            @return An affinity-wrapped awaitable.
        */
        template<typename Awaitable>
        auto
        await_transform(Awaitable&& a)
        {
            // Use if constexpr to get consistent return type per branch
            if constexpr (affine_awaitable<Awaitable, detail::executor_dispatcher>)
            {
                // Affine-aware: use affine_awaiter (zero overhead)
                return affine_awaiter{std::forward<Awaitable>(a), &await_dispatcher_};
            }
            else
            {
                // Legacy: use make_affine trampoline
                return make_affine(std::forward<Awaitable>(a), await_dispatcher_);
            }
        }

        /** Returns the task object for this coroutine.

            @return A task owning the coroutine handle.
        */
        task
        get_return_object()
        {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        /** Suspend the coroutine at the start.

            The coroutine is lazy and does not run until awaited.

            @return An awaitable that always suspends.
        */
        std::suspend_always
        initial_suspend() noexcept
        {
            return {};
        }

        /** Signal coroutine completion.

            Called when the coroutine executes `co_return;`.
        */
        void
        return_void() noexcept
        {
            error_ = nullptr;
        }

        /** Store an unhandled exception.

            Captures the current exception for later rethrowing.
        */
        void
        unhandled_exception() noexcept
        {
            error_ = std::current_exception();
        }

        /** Retrieve the result for await_resume.

            @throws Any exception that was thrown inside the coroutine.
        */
        void
        result()
        {
            if (error_)
                std::rethrow_exception(error_);
        }
    };

private:
    std::coroutine_handle<promise_type> h_;

public:
    /** Construct a task from a coroutine handle.

        @param h The coroutine handle to take ownership of.
    */
    explicit
    task(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }

    /** Destructor.

        Destroys the owned coroutine if present.
    */
    ~task()
    {
        if (h_)
            h_.destroy();
    }

    /** Move constructor.

        @param o The task to move from. After the move, @p o will
                 be empty.
    */
    task(task&& o) noexcept
        : h_(std::exchange(o.h_, {}))
    {
    }

    /// Move assignment is deleted.
    task&
    operator=(task&&) = delete;

    /** Access the underlying coroutine handle.

        @return The coroutine handle, without transferring ownership.
    */
    [[nodiscard]]
    std::coroutine_handle<promise_type>
    handle() const noexcept
    {
        return h_;
    }

    /** Release ownership of the coroutine handle.

        After calling this function, the task no longer owns the
        coroutine and the caller becomes responsible for destroying it.

        @return The coroutine handle.
    */
    [[nodiscard]]
    std::coroutine_handle<promise_type>
    release() noexcept
    {
        return std::exchange(h_, {});
    }

    /** Bind this task to an executor for affinity.

        Sets the executor so that when this task's internal
        co_await expressions complete, the coroutine resumes
        on the specified executor. Child tasks without explicit
        affinity will inherit this executor.

        @param e The executor to resume on.

        @return A reference to this task for chaining.

        @par Example
        @code
        task<void> example(executor ex)
        {
            // do_work resumes on ex after internal co_awaits
            co_await do_work().on(ex);
        }
        @endcode
    */
    task&
    on(executor ex) &
    {
        h_.promise().set_executor(std::move(ex));
        return *this;
    }

    /// @copydoc on(executor)
    task&&
    on(executor ex) &&
    {
        h_.promise().set_executor(std::move(ex));
        return std::move(*this);
    }
};

//-----------------------------------------------------------------------------

namespace detail {

/** Fire-and-forget coroutine for spawn().

    This coroutine runs the spawned task and delivers the result
    to the completion handler. It never suspends at final_suspend,
    so the frame is destroyed immediately upon completion.
*/
template<class T>
struct spawner
{
    struct promise_type
    {
        spawner
        get_return_object() noexcept
        {
            return {};
        }

        std::suspend_never
        initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_never
        final_suspend() noexcept
        {
            return {};
        }

        void
        return_void() noexcept
        {
        }

        void
        unhandled_exception()
        {
            // Handler is called with exception in spawn's try/catch
            std::terminate();
        }
    };
};

} // detail

/** Spawn a task on an executor with a completion handler.

    This function starts a task running on the specified executor.
    When the task completes (with a value or exception), the handler
    is invoked with the result.

    The handler receives `system::result<T, std::exception_ptr>` which
    holds either the task's return value or any exception that was
    thrown during execution.

    The coroutine frame is allocated using the executor's allocator.

    @param ex The executor to run the task on.
    @param t The task to spawn. Ownership is transferred.
    @param handler The completion handler to invoke with the result.

    @par Handler Signature
    @code
    void handler(system::result<T, std::exception_ptr> result);
    @endcode

    @par Example
    @code
    task<int> compute()
    {
        co_return 42;
    }

    void start_work(executor ex)
    {
        spawn(ex, compute(), [](auto result) {
            if (result.has_value())
                std::cout << "Result: " << *result << std::endl;
            else
                std::cerr << "Error occurred\n";
        });
    }
    @endcode

    @see task, executor, system::result
*/
template<class T, class Handler>
void
spawn(executor ex, task<T> t, Handler&& handler)
{
    using result_type = system::result<T, std::exception_ptr>;
    t.on(ex);
    auto do_spawn = [](
        task<T> t,
        std::decay_t<Handler> h) -> detail::spawner<void>
    {
#if defined(__GNUC__) && __GNUC__ >= 12 && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        try
        {
            h(result_type(co_await t));
        }
        catch (...)
        {
            h(result_type(std::current_exception()));
        }
#if defined(__GNUC__) && __GNUC__ >= 12 && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    };
    do_spawn(std::move(t), std::forward<Handler>(handler));
}

/** Spawn a void task on an executor with a completion handler.

    @copydetails spawn(executor,task<T>,Handler&&)
*/
template<class Handler>
void
spawn(executor ex, task<void> t, Handler&& handler)
{
    using result_type = system::result<void, std::exception_ptr>;
    t.on(ex);
    auto do_spawn = [](
        task<void> t,
        std::decay_t<Handler> h) -> detail::spawner<void>
    {
#if defined(__GNUC__) && __GNUC__ >= 12 && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
        try
        {
            co_await t;
            h(result_type());
        }
        catch (...)
        {
            h(result_type(std::current_exception()));
        }
#if defined(__GNUC__) && __GNUC__ >= 12 && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    };
    do_spawn(std::move(t), std::forward<Handler>(handler));
}

} // capy
} // boost

#endif

#endif
