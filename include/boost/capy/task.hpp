//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TASK_HPP
#define BOOST_CAPY_TASK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/io_awaitable_promise_base.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/io_result.hpp>

#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace boost {
namespace capy {

namespace detail {

// Helper base for result storage and return_void/return_value
template<typename T>
struct task_return_base
{
    std::optional<T> result_;

    void return_value(T value)
    {
        result_ = std::move(value);
    }

    T&& result() noexcept
    {
        return std::move(*result_);
    }
};

template<>
struct task_return_base<void>
{
    void return_void()
    {
    }
};

} // namespace detail

/** Defers a coroutine body until awaited, then runs it inline on the caller's thread.

    Use `task<T>` as the return type for coroutines that perform I/O
    and return a value of type `T`. The coroutine body does not start
    executing until the task is awaited, enabling efficient composition
    without unnecessary eager execution.

    The task participates in the I/O awaitable protocol: when awaited,
    it receives the caller's executor and stop token, propagating them
    to nested `co_await` expressions. This enables cancellation and
    proper completion dispatch across executor boundaries.

    @par Await-effects

    Let `t` be a `task<T>`. `co_await t` always suspends the awaiting
    coroutine, then transfers control directly into the task's coroutine
    body on the current thread; no executor operation is posted. The task
    records the caller's environment (executor, stop token, and frame
    allocator) by pointer rather than copying it. It propagates that
    environment to every `co_await` inside the body.

    The body runs until it returns or exits via an exception. Control
    then transfers directly back to the awaiting coroutine, again
    without an executor operation.

    `task` never inspects the stop token; it only propagates it. A task
    body observes a stop request through the results of the operations it
    awaits, or by reading the token itself. See @ref quitter for a task
    that stops its own body.

    @par Await-returns
    The value the body passed to `co_return`, moved out of the task, or
    nothing when `T` is `void`.

    If the body exits via an unhandled exception, that exception is
    rethrown instead.

    @par Await-postcondition
    The task's coroutine has run to completion and is suspended at its
    final suspend point. The task still owns the frame, but not the
    result: the await moves it out, so a task must not be awaited twice.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Unsafe.

    @par Example

    @code
    task<int> compute_value()
    {
        auto [ec, n] = co_await stream.read_some( buf );
        if( ec )
            co_return 0;
        co_return process( buf, n );
    }

    task<> run_session( tcp_socket sock )
    {
        int result = co_await compute_value();
        // ...
    }
    @endcode

    @tparam T The result type. Use `task<>` for `task<void>`.

    @see IoRunnable, IoAwaitable, run, run_async
*/
template<typename T = void>
struct [[nodiscard]] BOOST_CAPY_CORO_AWAIT_ELIDABLE
    task
{
    /** Stores `task<T>`'s result and joins the I/O awaitable protocol via `io_awaitable_promise_base`.

        This is the promise object the compiler associates with a
        `task<T>` coroutine. It satisfies the coroutine promise
        requirements and participates in the I/O awaitable protocol via
        @ref io_awaitable_promise_base. It is part of the coroutine
        machinery and is not intended to be used directly by callers.

        Result storage and `return_value`/`return_void` are provided by
        `detail::task_return_base<T>`.

        @see io_awaitable_promise_base, IoRunnable
    */
    struct promise_type
        : io_awaitable_promise_base<promise_type>
        , detail::task_return_base<T>
    {
    private:
        friend task;
        union { std::exception_ptr ep_; };
        bool has_ep_;

    public:
        /// Construct the promise with no stored exception.
        promise_type() noexcept
            : has_ep_(false)
        {
        }

        /// Destroy the promise, releasing any stored exception.
        ~promise_type()
        {
            if(has_ep_)
                ep_.~exception_ptr();
        }

        /** Return the exception captured by the coroutine body, if any.

            @return The stored exception, or a null `std::exception_ptr`
            if the coroutine did not exit via an unhandled exception.
        */
        std::exception_ptr exception() const noexcept
        {
            if(has_ep_)
                return ep_;
            return {};
        }

        /** Return the owning `task` for this coroutine.

            Called by the compiler to produce the object returned to the
            caller when the coroutine is created.

            @return A `task` owning the coroutine frame.
        */
        task get_return_object()
        {
            return task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        /** Return the initial-suspend awaiter.

            The coroutine always suspends at the initial suspend point,
            so the body does not start until the task is awaited. When the
            body is resumed, the awaiter restores the thread-local frame
            allocator from the stored environment.

            @return An awaiter that suspends unconditionally.
        */
        auto initial_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;

                bool await_ready() const noexcept
                {
                    return false;
                }

                void await_suspend(std::coroutine_handle<>) const noexcept
                {
                }

                void await_resume() const noexcept
                {
                    // Restore TLS when body starts executing
                    set_current_frame_allocator(p_->environment()->frame_allocator);
                }
            };
            return awaiter{this};
        }

        /** Return the final-suspend awaiter.

            The coroutine always suspends at the final suspend point. The
            awaiter's `await_suspend` performs symmetric transfer to the
            stored continuation (consuming it), resuming the awaiting
            coroutine.

            @return An awaiter that suspends and transfers to the
            continuation.
        */
        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;

                bool await_ready() const noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept
                {
                    return p_->continuation();
                }

                void await_resume() const noexcept {} // LCOV_EXCL_LINE final_suspend awaiter, never resumed
            };
            return awaiter{this};
        }

        /** Capture the in-flight exception from the coroutine body.

            Called by the compiler when the coroutine body exits via an
            unhandled exception. The captured exception is rethrown when
            the task is awaited.
        */
        void unhandled_exception() noexcept
        {
            new (&ep_) std::exception_ptr(std::current_exception());
            has_ep_ = true;
        }

        /** Awaiter wrapping a nested `co_await` of an @ref IoAwaitable.

            Forwards the environment to the inner awaitable's
            environment-taking `await_suspend` and restores the
            thread-local frame allocator before the body resumes.

            @tparam Awaitable The awaitable being transformed.
        */
        template<class Awaitable>
        struct transform_awaiter
        {
            /// The wrapped awaitable, decayed and stored by value.
            std::decay_t<Awaitable> a_;

            /// The promise of the coroutine performing the `co_await`.
            promise_type* p_;

            /** Report whether the wrapped awaitable is already complete.

                @return The wrapped awaitable's own `await_ready` result:
                `true` if no suspension is needed.
            */
            bool await_ready() noexcept
            {
                return a_.await_ready();
            }

            /** Restore the frame allocator, then resume the wrapped
                awaitable.

                Reinstalls the thread-local frame allocator from the stored
                environment before the body continues. This is needed
                because the resumption may arrive on a different thread
                than the one that suspended.

                @return The wrapped awaitable's await-result, forwarded
                unchanged.
            */
            decltype(auto) await_resume()
            {
                // Restore TLS before body resumes
                set_current_frame_allocator(p_->environment()->frame_allocator);
                return a_.await_resume();
            }

            /** Suspend by calling the wrapped awaitable with the
                environment.

                This is the plain `await_suspend` the compiler calls for the
                nested `co_await`. It forwards to the wrapped awaitable's
                @ref IoAwaitable overload, supplying the promise's stored
                environment as the second argument. It then hands back
                that call's result unchanged, so the wrapped awaitable's
                suspension decision, whatever form it takes, is preserved.

                @param h The coroutine performing the `co_await`.

                @return Whatever the wrapped awaitable's `await_suspend`
                returns. When that is a `std::coroutine_handle<>`, the
                handle is routed through `detail::symmetric_transfer`.
                On MSVC that helper resumes the handle on the current
                stack, and this function returns `void`, so the awaiting
                coroutine suspends unconditionally. On every other
                compiler the handle is returned unchanged for symmetric
                transfer.
            */
            template<class Promise>
            auto await_suspend(std::coroutine_handle<Promise> h) noexcept
            {
                using R = decltype(a_.await_suspend(h, p_->environment()));
                if constexpr (std::is_same_v<R, std::coroutine_handle<>>)
                    return detail::symmetric_transfer(a_.await_suspend(h, p_->environment()));
                else
                    return a_.await_suspend(h, p_->environment());
            }
        };

        /** Transform a nested awaitable before `co_await`.

            Wraps an @ref IoAwaitable in a @ref transform_awaiter so the
            coroutine's environment is propagated into it. A diagnostic
            is emitted if the awaitable does not satisfy @ref IoAwaitable.

            @param a The awaitable expression from `co_await a`.

            @return A @ref transform_awaiter wrapping `a`.
        */
        template<class Awaitable>
        auto transform_awaitable(Awaitable&& a)
        {
            using A = std::decay_t<Awaitable>;
            if constexpr (IoAwaitable<A>)
            {
                return transform_awaiter<Awaitable>{
                    std::forward<Awaitable>(a), this};
            }
            else
            {
                static_assert(sizeof(A) == 0, "requires IoAwaitable");
            }
        }
    };

    /** Handle to the owned coroutine frame.

        Null when the task is empty (for example after a move or after
        @ref release). Prefer @ref handle to read this; the member is
        public for use by the coroutine machinery.
    */
    std::coroutine_handle<promise_type> h_;

    /// Destroy the task and its coroutine frame if owned.
    ~task()
    {
        if(h_)
            h_.destroy();
    }

    /** Report whether the awaited task is already complete.

        Always returns `false`; a task is lazy and has not started when
        it is awaited, so the awaiting coroutine always suspends.

        @return `false`.
    */
    bool await_ready() const noexcept
    {
        return false;
    }

    /** Return the task's result, rethrowing any captured exception.

        If the coroutine body exited via an unhandled exception, that
        exception is rethrown here. Otherwise the result is returned by
        move (for `task<T>`) or nothing is returned (for `task<void>`).

        @return The result value for non-void `T`; otherwise `void`.

        @throws The exception captured by the coroutine body, if any.

        @note Discarding an `io_result` silently drops the error
        code, so that overload is marked `[[nodiscard]]`.
    */
    [[nodiscard]] auto await_resume()
        requires detail::is_io_result_v<T>
    {
        if(h_.promise().has_ep_)
            std::rethrow_exception(h_.promise().ep_);
        return std::move(*h_.promise().result_);
    }

    auto await_resume()
        requires (! detail::is_io_result_v<T>)
    {
        if(h_.promise().has_ep_)
            std::rethrow_exception(h_.promise().ep_);
        if constexpr (! std::is_void_v<T>)
            return std::move(*h_.promise().result_);
        else
            return;
    }

    /** Start the task with the awaiting coroutine's context.

        Stores `cont` as the continuation to resume on completion.
        Stores `env` as the execution environment propagated to nested
        `co_await` expressions. Then transfers control into the task's
        coroutine body via the returned handle.

        @param cont The awaiting coroutine to resume when the task
        completes.

        @param env The execution environment (executor, stop token, and
        frame allocator). It must outlive the task.

        @return The task's coroutine handle, for symmetric transfer.
    */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont, io_env const* env)
    {
        h_.promise().set_continuation(cont);
        h_.promise().set_environment(env);
        return h_;
    }

    /** Return the coroutine handle.

        @note Do not call `destroy()` on the returned handle while the
        task is being awaited. The task's lifetime is normally managed
        by `run_async`, `run`, or the awaiting parent. Manually
        destroying a suspended task that another coroutine is awaiting
        produces undefined behavior. For cooperative cancellation, use
        `std::stop_token`.

        @return The coroutine handle.
    */
    std::coroutine_handle<promise_type> handle() const noexcept
    {
        return h_;
    }

    /** Release ownership of the coroutine frame.

        After calling this, destroying the task does not destroy the
        coroutine frame. The caller becomes responsible for the frame's
        lifetime.

        @note The caller may call `destroy()` on the released handle
        only when the task has not started or has fully completed.
        Destroying a suspended task that is being awaited produces
        undefined behavior.

        @par Postconditions
        `handle()` returns a null handle. Callers needing the
        original handle must save it, via @ref handle, before
        calling this.
    */
    void release() noexcept
    {
        h_ = nullptr;
    }

    /** Copy construction is disabled; a task uniquely owns its frame.

        @param other The task that would be copied.
    */
    task(task const& other) = delete;

    /** Copy assignment is disabled; a task uniquely owns its frame.

        @param other The task that would be assigned from.

        @return A reference to `*this`.
    */
    task& operator=(task const& other) = delete;

    /** Construct by moving, transferring ownership of the frame.

        @par Postconditions
        `other` is empty and must not be awaited.

        @param other The task to move from.
    */
    task(task&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    /** Assign by moving, transferring ownership of the frame.

        If this task already owns a coroutine frame, that frame is
        destroyed first. Self-assignment is a no-op.

        @par Postconditions
        `other` is empty and must not be awaited.

        @param other The task to move from.

        @return A reference to `*this`.
    */
    task& operator=(task&& other) noexcept
    {
        if(this != &other)
        {
            if(h_)
                h_.destroy();
            h_ = std::exchange(other.h_, nullptr);
        }
        return *this;
    }

private:
    explicit task(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
};

} // namespace capy
} // namespace boost

#endif
