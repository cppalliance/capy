//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_QUITTER_HPP
#define BOOST_CAPY_QUITTER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/stop_requested_exception.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/context_awaitable.hpp>
#include <boost/capy/ex/context_awaitable_promise_base.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>

#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

/* Stop-aware coroutine task.

   quitter<T> is identical to task<T> except that a stop request ends
   the body without the body checking an error code.  The promise's
   transform_awaiter checks the stop token before it touches an awaited
   operation, and again when an operation that completed
   asynchronously resumes the body.  A pending stop there throws a
   sentinel exception that unwinds through RAII destructors to
   final_suspend, and the parent sees a "stopped" completion.  An
   operation that completes synchronously always has its await_resume
   called, because its await_ready or await_suspend may already have
   acquired something.  A results<> it returns holding cond::canceled
   while the stop is pending acquired nothing, so the stop throws there;
   any other result reaches the body, and the stop takes effect at the
   next co_await.

   See doc/quitter.md for the full design rationale. */

namespace boost {
namespace capy {

namespace detail {

// Reuse the same return-value storage as task<T>.
// task_return_base is defined in task.hpp, but quitter needs its own
// copy to avoid a header dependency on task.hpp.
template<typename T>
struct quitter_return_base
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
struct quitter_return_base<void>
{
    void return_void()
    {
    }
};

} // namespace detail

/** Defers a coroutine body until awaited, then unwinds it early on a stop request.

    When the stop token is triggered, the body is unwound at a
    `co_await` instead of resuming, and RAII destructors run normally.
    Await-effects below says at which one.  The parent observes a "stopped"
    completion via @ref promise_type::stopped.

    Everything else — frame allocation, environment propagation,
    symmetric transfer, move semantics — is identical to @ref task.

    @par Await-effects

    Let `q` be a `quitter<T>`. `co_await q` always suspends the awaiting
    coroutine, then transfers control directly into the quitter's
    coroutine body on the current thread; no executor operation is
    posted. The quitter records the caller's environment (executor, stop
    token, and frame allocator) by pointer rather than copying it. It
    propagates that environment to every `co_await` inside the body.

    Unlike @ref task, the stop token is checked before the body's first
    statement, before each awaited operation is started, and when an
    operation that completed asynchronously resumes the body. If a stop
    request is pending at those points, the body is not resumed. An
    internal sentinel exception unwinds it instead, so RAII destructors
    run, and the coroutine completes as stopped.

    An awaited operation that completes synchronously always has its
    `await_resume` called, even if a stop request arrives at the same
    time. Its `await_ready` or `await_suspend` may already have acquired
    a resource, such as `async_mutex`'s lock, that only its
    `await_resume` hands over. If that result is a @ref results holding
    `cond::canceled` while a stop is pending, the operation saw the stop
    first and acquired nothing, so the body is unwound there. Any other
    result is handed to the body, and the stop takes effect at the next
    `co_await`. An operation whose result is not a @ref results can
    therefore hand the body one result it produced after seeing the
    stop.

    The body runs until it returns, exits via an exception, or is unwound
    by a stop request. Control then transfers directly back to the
    awaiting coroutine, again without an executor operation.

    @par Await-returns
    The value the body passed to `co_return`, moved out of the quitter,
    or nothing when `T` is `void`.

    If the body exits via an unhandled exception, that exception is
    rethrown instead.

    If the coroutine completed as stopped, the internal sentinel
    exception is thrown instead of await-returning. Awaiting a stopped
    `quitter` from another `quitter` therefore stops that one too. A
    @ref task awaiting it sees the sentinel as an unhandled exception in
    its own body. When a quitter is started by `run_async`, a stopped
    completion reaches the error handler as the sentinel
    `std::exception_ptr`, not the value handler.

    @par Await-postcondition
    The quitter's coroutine has run to completion and is suspended at its
    final suspend point; the body's RAII destructors have run. Exactly
    one of the following holds: the body returned a value; the body
    exited via an exception; or `handle().promise().stopped()` returns
    `true`. When the body returned a value, the await moved it out, so a
    quitter must not be awaited twice.

    @par Remarks
    Supports _ContextAwaitable cancellation_.

    @tparam T The result type.  Use `quitter<>` for `quitter<void>`.

    @see @ref task, @ref ContextRunnable, @ref ContextAwaitable
*/
template<typename T = void>
struct [[nodiscard]] BOOST_CAPY_CORO_AWAIT_ELIDABLE
    quitter
{
    /** Stores `quitter<T>`'s result and unwinds the body when the stop token fires.

        This is the promise object the compiler associates with a
        `quitter<T>` coroutine. It satisfies the coroutine promise
        requirements and participates in the I/O awaitable protocol via
        @ref context_awaitable_promise_base. Unlike @ref task::promise_type,
        its `transform_awaitable` checks the stop token before each
        awaited result reaches the body. A pending stop request throws an
        internal sentinel exception that unwinds to a "stopped"
        completion. It is part of the coroutine machinery and is not
        intended to be used directly by callers.

        Result storage and `return_value`/`return_void` are provided by
        `detail::quitter_return_base<T>`.

        @see @ref context_awaitable_promise_base, @ref ContextRunnable
    */
    struct promise_type
        : context_awaitable_promise_base<promise_type>
        , detail::quitter_return_base<T>
    {
    private:
        friend quitter;

        enum class completion { running, value, exception, stopped };

        union { std::exception_ptr ep_; };
        completion state_;

    public:
        /// Construct the promise in the running state.
        promise_type() noexcept
            : state_(completion::running)
        {
        }

        /// Destroy the promise, releasing any stored exception.
        ~promise_type()
        {
            if(state_ == completion::exception ||
               state_ == completion::stopped)
                ep_.~exception_ptr();
        }

        /** Return a non-null exception_ptr when the coroutine threw
            or was stopped.

            Stopped quitters report the sentinel
            stop_requested_exception so that run_async routes to
            the error handler instead of accessing a non-existent
            result.

            @return The stored exception if the coroutine exited via an
            exception or was stopped, otherwise a null
            `std::exception_ptr`.
        */
        std::exception_ptr exception() const noexcept
        {
            if(state_ == completion::exception ||
               state_ == completion::stopped)
                return ep_;
            return {};
        }

        /** True when the coroutine was stopped via the stop token.

            @return `true` if the body was unwound by a stop request;
            `false` if it returned a value or exited via any other
            exception.
        */
        bool stopped() const noexcept
        {
            return state_ == completion::stopped;
        }

        /** Return the owning `quitter` for this coroutine.

            Called by the compiler to produce the object returned to the
            caller when the coroutine is created.

            @return A `quitter` owning the coroutine frame.
        */
        quitter get_return_object()
        {
            return quitter{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        /** Return the initial-suspend awaiter.

            The coroutine always suspends at the initial suspend point,
            so the body does not start until the quitter is awaited. When
            the body is resumed, the awaiter restores the thread-local
            frame allocator. It then throws the internal sentinel
            exception if stop is already requested, so the body never
            runs and the coroutine completes as stopped.

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

                // Potentially-throwing: checks the stop token before
                // the coroutine body executes its first statement.
                void await_resume() const
                {
                    set_current_frame_allocator(
                        p_->environment()->frame_allocator);
                    if(p_->environment()->stop_token.stop_requested())
                        throw detail::stop_requested_exception{};
                }
            };
            return awaiter{this};
        }

        /** Return the final-suspend awaiter.

            The coroutine always suspends at the final suspend point. The
            awaiter's `await_suspend` performs symmetric transfer to the
            stored continuation, resuming the awaiting coroutine.

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

                std::coroutine_handle<> await_suspend(
                    std::coroutine_handle<>) const noexcept
                {
                    return p_->continuation();
                }

                void await_resume() const noexcept {} // LCOV_EXCL_LINE final_suspend awaiter, never resumed
            };
            return awaiter{this};
        }

        /** Capture the in-flight exception from the coroutine body.

            Called by the compiler when the coroutine body exits via an
            unhandled exception. The internal stop sentinel is recorded as
            a stopped completion; any other exception is recorded as an
            exception completion. The stored exception is surfaced (or
            routed to the error handler) when the quitter is awaited or run.
        */
        void unhandled_exception()
        {
            try
            {
                throw;
            }
            catch(detail::stop_requested_exception const&)
            {
                // Store the exception_ptr so that run_async's
                // invoke_impl routes to the error handler
                // instead of accessing a non-existent result.
                new (&ep_) std::exception_ptr(
                    std::current_exception());
                state_ = completion::stopped;
            }
            catch(...)
            {
                new (&ep_) std::exception_ptr(
                    std::current_exception());
                state_ = completion::exception;
            }
        }

        //------------------------------------------------------
        // transform_awaitable — the key difference from task<T>
        //------------------------------------------------------

        /** Awaiter wrapping a nested `co_await` of an @ref ContextAwaitable.

            Forwards the environment to the inner awaitable's
            environment-taking `await_suspend` and restores the
            thread-local frame allocator before the body resumes. Unlike
            `task`'s, it also checks the stop token. A stop pending before
            the wrapped awaitable is touched, or on resumption after an
            asynchronous completion, throws the internal sentinel. The
            body then unwinds before it observes the operation's result.
            A synchronous completion is always handed over through the
            wrapped awaitable's `await_resume`.

            @tparam Awaitable The awaitable being transformed.
        */
        template<class Awaitable>
        struct transform_awaiter
        {
            /// The wrapped awaitable, decayed and stored by value.
            std::decay_t<Awaitable> a_;

            /// The promise of the coroutine performing the `co_await`.
            promise_type* p_;

            /// True when a stop was pending at the `co_await`, so the
            /// wrapped awaitable was never touched.
            bool skipped_ = false;

            /// True when the wrapped awaitable completed synchronously, so
            /// its `await_resume` must run to hand over what it acquired.
            bool completed_ = false;

            /** Report whether the `co_await` can continue without
                suspending.

                The stop token is checked first. If a stop is pending, the
                wrapped awaitable is never touched: this returns `true`, and
                @ref await_resume throws. Otherwise the wrapped awaitable's
                own `await_ready` decides. A `true` result there means it
                completed synchronously, so @ref await_resume calls its
                `await_resume` even if a stop arrives afterwards. That hands
                over anything it acquired, such as `async_mutex`'s lock.

                Where `BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND` is on,
                this always returns `false`, and @ref await_suspend does both
                steps instead. See `detail::await_ready_then_suspend`.

                @return `true` if no suspension is needed. Always `false`
                where the workaround is on.
            */
            bool await_ready() noexcept
            {
#if BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND
                return false;
#else
                if(p_->environment()->stop_token.stop_requested())
                {
                    skipped_ = true;
                    return true;
                }
                completed_ = a_.await_ready();
                return completed_;
#endif
            }

            /** Restore the frame allocator, check for stop, then resume the
                wrapped awaitable.

                Reinstalls the thread-local frame allocator from the stored
                environment. The internal sentinel exception is then thrown
                in three cases: a stop was pending before the wrapped
                awaitable was touched; a stop is pending now and the
                wrapped awaitable completed asynchronously; or a stop is
                pending now and the wrapped awaitable's result is a
                @ref results holding `cond::canceled`. The body therefore
                never observes that operation's result, and unwinds through
                its RAII destructors to a stopped completion. A wrapped
                awaitable that completed synchronously always has its
                `await_resume` called, and any other result it gives is
                returned, with the stop taking effect at the next
                `co_await`. This is the one place `quitter` differs from
                @ref task::promise_type::transform_awaiter.

                After an asynchronous completion with a stop pending, the
                operation's result is dropped. Releasing anything it held is
                then the wrapped awaiter's destructor's job: `async_mutex`
                releases a lock it granted, and `async_waker` drops the
                wakeup. The body is unwinding, so nothing observes the loss.

                @return The wrapped awaitable's await-result, forwarded
                unchanged, when no sentinel is thrown.

                @par Exception Safety
                Throws the library's internal stop sentinel in the three
                cases above. The wrapped awaitable's `await_resume` is not
                called in the first two; in the third, its canceled result
                is dropped.
            */
            decltype(auto) await_resume()
            {
                set_current_frame_allocator(
                    p_->environment()->frame_allocator);
                if(skipped_ ||
                    (!completed_ &&
                     p_->environment()->stop_token.stop_requested()))
                    throw detail::stop_requested_exception{};
                using R = decltype(a_.await_resume());
                if constexpr (detail::is_results_v<R>)
                {
                    // A result canceled by our own stop acquired
                    // nothing, so the stop can take effect here.
                    R r = a_.await_resume();
                    if(std::get<0>(r) == cond::canceled &&
                        p_->environment()->stop_token.stop_requested())
                        throw detail::stop_requested_exception{};
                    return r;
                }
                else
                {
                    return a_.await_resume();
                }
            }

            /** Suspend by calling the wrapped awaitable with the
                environment.

                This is the plain `await_suspend` the compiler calls for the
                nested `co_await`. It forwards to the wrapped awaitable's
                @ref ContextAwaitable overload, supplying the promise's stored
                environment as the second argument. It keeps the wrapped
                awaitable's suspension decision, whatever form it takes.
                When that decision is to continue without suspending, the
                wrapped awaitable completed synchronously, and
                @ref await_resume will call its `await_resume`. Returning
                `h` itself counts as continuing without suspending.

                @param h The coroutine performing the `co_await`.

                @return Whatever the wrapped awaitable's `await_suspend`
                returns. When that is a `std::coroutine_handle<>`, the
                handle is routed through `detail::symmetric_transfer`.
                Where `BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND` is on,
                this function instead returns `bool`. It first checks the
                stop token, as @ref await_ready does where the workaround is
                off, and returns `false` without touching the wrapped
                awaitable if a stop is pending. Otherwise it returns the
                result of `detail::await_ready_then_suspend`. Where the
                workaround is off, a returned handle is passed back
                unchanged for symmetric transfer.
            */
            template<class Promise>
            auto await_suspend(
                std::coroutine_handle<Promise> h) noexcept
            {
                // completed_ is written only where nothing else can have
                // resumed this frame: a frame that really suspended may
                // already be running, or gone, on another thread.
#if BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND
                if(p_->environment()->stop_token.stop_requested())
                {
                    skipped_ = true;
                    return false;
                }
                return detail::await_ready_then_suspend(
                    a_, h, p_->environment(), &completed_);
#else
                using R = decltype(
                    a_.await_suspend(h, p_->environment()));
                if constexpr (std::is_same_v<
                    R, std::coroutine_handle<>>)
                {
                    std::coroutine_handle<> next =
                        a_.await_suspend(h, p_->environment());
                    // Returning h means no one else can resume this
                    // frame: a synchronous completion.
// GCC falsely warns that writing completed_ overruns the noop
// coroutine's frame: when the wrapped await_suspend returns
// std::noop_coroutine(), it assumes the branch below can be taken
// and treats this frame as the noop frame. A noop handle never
// equals h, so the branch is dead on that path.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
                    if(next == h)
                        completed_ = true;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
                    return detail::symmetric_transfer(next);
                }
                else if constexpr (std::is_same_v<R, bool>)
                {
                    if(!a_.await_suspend(h, p_->environment()))
                    {
                        completed_ = true;
                        return false;
                    }
                    return true;
                }
                else
                {
                    return a_.await_suspend(
                        h, p_->environment());
                }
#endif
            }
        };

        /** Transform a nested awaitable before `co_await`.

            Wraps an @ref ContextAwaitable in a @ref transform_awaiter so the
            coroutine's environment is propagated into it and the stop
            token is checked on resumption. A diagnostic is emitted if the
            awaitable does not satisfy @ref ContextAwaitable.

            @param a The awaitable expression from `co_await a`.

            @return A @ref transform_awaiter wrapping `a`.
        */
        template<class Awaitable>
        auto transform_awaitable(Awaitable&& a)
        {
            using A = std::decay_t<Awaitable>;
            if constexpr (ContextAwaitable<A>)
            {
                return transform_awaiter<Awaitable>{
                    std::forward<Awaitable>(a), this};
            }
            else
            {
                static_assert(sizeof(A) == 0,
                    "requires ContextAwaitable");
            }
        }
    };

    /** Handle to the owned coroutine frame.

        Null when the quitter is empty (for example after a move or after
        @ref release). Prefer @ref handle to read this; the member is
        public for use by the coroutine machinery.
    */
    std::coroutine_handle<promise_type> h_;

    /** Destroy the quitter and its coroutine frame if owned.

        @par Preconditions
        The frame is not lent: no parent is suspended awaiting it,
        unless its chain is being unwound after an @ref abandon.
        Destroying a lent frame otherwise calls `std::terminate`; end
        a running chain by requesting stop, or by destroying its
        execution context.
    */
    ~quitter()
    {
        destroy_frame();
    }

    /** Return false; quitters are never immediately ready.

        A quitter is lazy and has not started when it is awaited, so the
        awaiting coroutine always suspends.

        @return `false`.
    */
    bool await_ready() const noexcept
    {
        return false;
    }

    /** Return the result, rethrow exception, or propagate stop.

        When stopped, throws stop_requested_exception so that a
        parent quitter also stops.  A parent task<T> sees this
        as an unhandled exception — by design.

        @return The result value for non-void `T`, moved out of the
        quitter; otherwise `void`.

        @par Exception Safety
        If the coroutine was stopped, the library's internal stop sentinel
        is thrown. If the body exited via any other exception, that
        exception is rethrown.
    */
    auto await_resume()
    {
        lent_ = false;
        if(h_.promise().stopped())
            throw detail::stop_requested_exception{};
        if(h_.promise().state_ == promise_type::completion::exception)
            std::rethrow_exception(h_.promise().ep_);
        if constexpr (! std::is_void_v<T>)
            return std::move(*h_.promise().result_);
        else
            return;
    }

    /** Start execution with the caller's context.

        Stores `cont` as the continuation to resume on completion.
        Stores `env` as the execution environment propagated to nested
        `co_await` expressions. Then transfers control into the quitter's
        coroutine body via the returned handle.

        @param cont The awaiting coroutine to resume when the quitter
        completes.

        @param env The execution environment (executor, stop token, and
        frame allocator). It must outlive the quitter.

        @return The quitter's coroutine handle, for symmetric transfer.
    */
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> cont,
        await_context const* env)
    {
        h_.promise().set_continuation(cont);
        h_.promise().set_environment(env);
        lent_ = true;
        return h_;
    }

    /** Return the coroutine handle.

        @note Do not call `destroy()` on the returned handle while the
        quitter is being awaited: the awaiting frame still holds it and
        destroys it again, which has undefined behavior. A holder that
        gives the coroutine up passes its continuation to @ref abandon
        instead. (Destroying or
        move-assigning over the quitter object itself while its frame is
        lent is a different operation, and calls `std::terminate`.)
        The quitter's lifetime is normally managed by `run_async`, `run`,
        or the awaiting parent. For cooperative cancellation, use
        `std::stop_token`.

        @note The operation, queue or primitive holding a suspended
        coroutine gives it up by passing the continuation it keeps to
        @ref abandon. `handle()` belongs to the owning quitter object.

        @return The coroutine handle.
    */
    std::coroutine_handle<promise_type> handle() const noexcept
    {
        return h_;
    }

    /** Release ownership of the coroutine frame.

        @note The caller may call `destroy()` on the released handle
        only when the quitter has not started or has fully completed.
        Destroying the released handle of a quitter that is being
        awaited is not supported and has undefined behavior.

        @par Postconditions
        `handle()` returns a null handle. Callers needing the
        original handle must save it, via @ref handle, before
        calling this.
    */
    void release() noexcept
    {
        h_ = nullptr;
    }

    /** Copy construction is disabled; a quitter uniquely owns its frame.

        @param other The quitter that would be copied.
    */
    quitter(quitter const& other) = delete;

    /** Copy assignment is disabled; a quitter uniquely owns its frame.

        @param other The quitter that would be assigned from.

        @return A reference to `*this`.
    */
    quitter& operator=(quitter const& other) = delete;

    /** Construct by moving, transferring ownership.

        @par Postconditions
        `other` is empty and must not be awaited.

        @param other The quitter to move from.
    */
    quitter(quitter&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    /** Assign by moving, transferring ownership.

        If this quitter already owns a coroutine frame, that frame is
        destroyed first. Self-assignment is a no-op.

        @par Preconditions
        The frame being replaced is not lent: no parent is suspended
        awaiting it. Replacing a lent frame calls `std::terminate`.

        @par Postconditions
        `other` is empty and must not be awaited.

        @param other The quitter to move from.

        @return A reference to `*this`.
    */
    quitter& operator=(quitter&& other) noexcept
    {
        if(this != &other)
        {
            destroy_frame();
            h_ = std::exchange(other.h_, nullptr);
        }
        return *this;
    }

private:
    // Set while a parent is suspended awaiting this quitter.
    bool lent_ = false;

    explicit quitter(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }

    void destroy_frame() noexcept
    {
        if(!h_)
            return;
        if(lent_ && !h_.done())
        {
            // Only the unwind of an abandoned chain destroys a frame
            // that is still awaited.
            auto* owner = h_.promise().environment()->owner;
            if(!owner || !owner->abandoned())
                std::terminate(); // LCOV_EXCL_LINE reached only in a death test's child process
        }
        std::exchange(h_, nullptr).destroy();
        lent_ = false;
    }
};

} // namespace capy
} // namespace boost

#endif
