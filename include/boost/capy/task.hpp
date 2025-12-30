//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TASK_HPP
#define BOOST_CAPY_TASK_HPP

#include <boost/capy/detail/config.hpp>

#ifdef BOOST_CAPY_HAS_CORO

#include <boost/capy/executor.hpp>
#include <boost/capy/make_affine.hpp>

#include <coroutine>
#include <exception>
#include <functional>
#include <utility>
#include <variant>

namespace boost {
namespace capy {

/// Type-erased dispatcher function signature
using dispatcher_type = std::function<void(std::function<void()>)>;

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

    @see async_result
*/
template<class T>
class task
{
public:
    /** The coroutine promise type.

        This nested type satisfies the coroutine promise requirements
        and manages the coroutine's result storage and completion
        notification.
    */
    struct promise_type
    {
        /// Storage for the result value or exception
        std::variant<std::monostate, T, std::exception_ptr> result{};

        /// Callback invoked when the coroutine completes
        std::function<void()> on_done;

        /// Dispatcher for executor affinity
        dispatcher_type dispatcher = [](auto f){ f(); };

        /** Transform awaitables for executor affinity.

            Wraps all co_await expressions with make_affine to ensure
            the coroutine resumes via the configured dispatcher.

            @param a The awaitable to transform.
            @return An affinity-wrapped awaitable.
        */
        template<typename Awaitable>
        auto await_transform(Awaitable&& a)
        {
            return make_affine(std::forward<Awaitable>(a), dispatcher);
        }

        /** Returns the task object for this coroutine.

            @return A task owning the coroutine handle.
        */
        task get_return_object()
        {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        /** Suspend the coroutine at the start.

            The coroutine is lazy and does not run until awaited.

            @return An awaitable that always suspends.
        */
        std::suspend_always initial_suspend() noexcept { return {}; }

        /** Suspend at the end and notify completion.

            @return An awaitable that suspends and invokes the
                    completion callback if set.
        */
        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() noexcept { return false; }
                void await_suspend(std::coroutine_handle<>) noexcept
                {
                    if (p_->on_done)
                        p_->on_done();
                }
                void await_resume() noexcept {}
            };
            return awaiter{this};
        }

        /** Store the return value.

            @param v The value to store as the coroutine result.
        */
        void return_value(T v) { result.template emplace<1>(std::move(v)); }

        /** Store an unhandled exception.

            Captures the current exception for later rethrowing.
        */
        void unhandled_exception() { result.template emplace<2>(std::current_exception()); }
    };

private:
    std::coroutine_handle<promise_type> h_;

public:
    /** Construct a task from a coroutine handle.

        @param h The coroutine handle to take ownership of.
    */
    explicit task(std::coroutine_handle<promise_type> h) : h_(h) {}

    /** Destructor.

        Destroys the owned coroutine if present.
    */
    ~task() { if (h_) h_.destroy(); }

    /** Move constructor.

        @param o The task to move from. After the move, @p o will
                 be empty.
    */
    task(task&& o) noexcept : h_(std::exchange(o.h_, {})) {}

    /// Move assignment is deleted.
    task& operator=(task&&) = delete;

    /** Check if the task is ready.

        @return Always returns false; the task must be awaited.
    */
    bool await_ready() const noexcept { return false; }

    /** Suspend the caller and start this task.

        Sets up the completion callback to resume the caller
        when this task completes, then transfers control to
        this task's coroutine.

        @param caller The coroutine handle of the awaiting coroutine.

        @return The coroutine handle to resume (this task's handle).
    */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept
    {
        h_.promise().on_done = [caller]{ caller.resume(); };
        return h_;
    }

    /** Retrieve the result after completion.

        @return The value produced by the coroutine.

        @throws Any exception that was thrown inside the coroutine.
    */
    [[nodiscard]]
    T await_resume()
    {
        auto& r = h_.promise().result;
        if (r.index() == 2)
            std::rethrow_exception(std::get<2>(r));
        return std::move(std::get<1>(r));
    }

    /** Access the underlying coroutine handle.

        @return The coroutine handle, without transferring ownership.
    */
    [[nodiscard]]
    std::coroutine_handle<promise_type> handle() const noexcept { return h_; }

    /** Release ownership of the coroutine handle.

        After calling this function, the task no longer owns the
        coroutine and the caller becomes responsible for destroying it.

        @return The coroutine handle.
    */
    [[nodiscard]]
    std::coroutine_handle<promise_type> release() noexcept
    {
        return std::exchange(h_, {});
    }

    /** Bind this task to an executor for affinity.

        Sets the dispatcher so that when this task's internal
        co_await expressions complete, the coroutine resumes
        on the specified executor.

        @param ex The executor to resume on.

        @return A reference to this task for chaining.

        @par Example
        @code
        task<void> example(executor pool)
        {
            // parse_request resumes on pool after internal co_awaits
            auto data = co_await parse_request().on(pool);
        }
        @endcode
    */
    task& on(executor ex) &
    {
        h_.promise().dispatcher = [ex](auto f) mutable {
            ex.post(std::move(f));
        };
        return *this;
    }

    /// @copydoc on(executor)
    task&& on(executor ex) &&
    {
        h_.promise().dispatcher = [ex](auto f) mutable {
            ex.post(std::move(f));
        };
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

    @see task, async_result
*/
template<>
class task<void>
{
public:
    /** The coroutine promise type for void tasks.

        This nested type satisfies the coroutine promise requirements
        and manages exception storage and completion notification.
    */
    struct promise_type
    {
        /// Storage for an exception, if one was thrown
        std::exception_ptr exception_{};

        /// Callback invoked when the coroutine completes
        std::function<void()> on_done;

        /// Dispatcher for executor affinity
        dispatcher_type dispatcher = [](auto f){ f(); };

        /** Transform awaitables for executor affinity.

            Wraps all co_await expressions with make_affine to ensure
            the coroutine resumes via the configured dispatcher.

            @param a The awaitable to transform.
            @return An affinity-wrapped awaitable.
        */
        template<typename Awaitable>
        auto await_transform(Awaitable&& a)
        {
            return make_affine(std::forward<Awaitable>(a), dispatcher);
        }

        /** Returns the task object for this coroutine.

            @return A task owning the coroutine handle.
        */
        task get_return_object()
        {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        /** Suspend the coroutine at the start.

            The coroutine is lazy and does not run until awaited.

            @return An awaitable that always suspends.
        */
        std::suspend_always initial_suspend() noexcept { return {}; }

        /** Suspend at the end and notify completion.

            @return An awaitable that suspends and invokes the
                    completion callback if set.
        */
        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() noexcept { return false; }
                void await_suspend(std::coroutine_handle<>) noexcept
                {
                    if (p_->on_done)
                        p_->on_done();
                }
                void await_resume() noexcept {}
            };
            return awaiter{this};
        }

        /** Signal coroutine completion.

            Called when the coroutine executes `co_return;`.
        */
        void return_void() noexcept {}

        /** Store an unhandled exception.

            Captures the current exception for later rethrowing.
        */
        void unhandled_exception() { exception_ = std::current_exception(); }
    };

private:
    std::coroutine_handle<promise_type> h_;

public:
    /** Construct a task from a coroutine handle.

        @param h The coroutine handle to take ownership of.
    */
    explicit task(std::coroutine_handle<promise_type> h) : h_(h) {}

    /** Destructor.

        Destroys the owned coroutine if present.
    */
    ~task() { if (h_) h_.destroy(); }

    /** Move constructor.

        @param o The task to move from. After the move, @p o will
                 be empty.
    */
    task(task&& o) noexcept : h_(std::exchange(o.h_, {})) {}

    /// Move assignment is deleted.
    task& operator=(task&&) = delete;

    /** Check if the task is ready.

        @return Always returns false; the task must be awaited.
    */
    bool await_ready() const noexcept { return false; }

    /** Suspend the caller and start this task.

        Sets up the completion callback to resume the caller
        when this task completes, then transfers control to
        this task's coroutine.

        @param caller The coroutine handle of the awaiting coroutine.

        @return The coroutine handle to resume (this task's handle).
    */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept
    {
        h_.promise().on_done = [caller]{ caller.resume(); };
        return h_;
    }

    /** Complete the await operation.

        @throws Any exception that was thrown inside the coroutine.
    */
    void await_resume()
    {
        if (h_.promise().exception_)
            std::rethrow_exception(h_.promise().exception_);
    }

    /** Access the underlying coroutine handle.

        @return The coroutine handle, without transferring ownership.
    */
    [[nodiscard]]
    std::coroutine_handle<promise_type> handle() const noexcept { return h_; }

    /** Release ownership of the coroutine handle.

        After calling this function, the task no longer owns the
        coroutine and the caller becomes responsible for destroying it.

        @return The coroutine handle.
    */
    [[nodiscard]]
    std::coroutine_handle<promise_type> release() noexcept
    {
        return std::exchange(h_, {});
    }

    /** Bind this task to an executor for affinity.

        Sets the dispatcher so that when this task's internal
        co_await expressions complete, the coroutine resumes
        on the specified executor.

        @param ex The executor to resume on.

        @return A reference to this task for chaining.

        @par Example
        @code
        task<void> example(executor pool)
        {
            // do_work resumes on pool after internal co_awaits
            co_await do_work().on(pool);
        }
        @endcode
    */
    task& on(executor ex) &
    {
        h_.promise().dispatcher = [ex](auto f) mutable {
            ex.post(std::move(f));
        };
        return *this;
    }

    /// @copydoc on(executor)
    task&& on(executor ex) &&
    {
        h_.promise().dispatcher = [ex](auto f) mutable {
            ex.post(std::move(f));
        };
        return std::move(*this);
    }
};

} // capy
} // boost

#endif

#endif
