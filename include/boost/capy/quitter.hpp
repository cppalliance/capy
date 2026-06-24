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
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/io_awaitable_promise_base.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>

#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

/* Stop-aware coroutine task.

   quitter<T> is identical to task<T> except that when the stop token
   is triggered, the coroutine body never sees the cancellation.  The
   promise intercepts it on resume (in transform_awaiter::await_resume)
   and throws a sentinel exception that unwinds through RAII destructors
   to final_suspend.  The parent sees a "stopped" completion.

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

/** Stop-aware lazy coroutine task satisfying @ref IoRunnable.

    When the stop token is triggered, the next `co_await` inside the
    coroutine short-circuits: the body never sees the result and RAII
    destructors run normally.  The parent observes a "stopped"
    completion via @ref promise_type::stopped.

    Everything else — frame allocation, environment propagation,
    symmetric transfer, move semantics — is identical to @ref task.

    @tparam T The result type.  Use `quitter<>` for `quitter<void>`.

    @see task, IoRunnable, IoAwaitable
*/
template<typename T = void>
struct [[nodiscard]] BOOST_CAPY_CORO_AWAIT_ELIDABLE
    quitter
{
    /** The coroutine promise type for `quitter<T>`.

        This is the promise object the compiler associates with a
        `quitter<T>` coroutine. It satisfies the coroutine promise
        requirements and participates in the I/O awaitable protocol via
        @ref io_awaitable_promise_base. Unlike @ref task::promise_type,
        its `transform_awaitable` checks the stop token before each
        awaited result reaches the body, throwing an internal sentinel
        exception that unwinds to a "stopped" completion. It is part of
        the coroutine machinery and is not intended to be used directly
        by callers.

        Result storage and `return_value`/`return_void` are provided by
        `detail::quitter_return_base<T>`.

        @see io_awaitable_promise_base, IoRunnable
    */
    struct promise_type
        : io_awaitable_promise_base<promise_type>
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

        /// Return a non-null exception_ptr when the coroutine threw
        /// or was stopped.  Stopped quitters report the sentinel
        /// stop_requested_exception so that run_async routes to
        /// the error handler instead of accessing a non-existent
        /// result.
        std::exception_ptr exception() const noexcept
        {
            if(state_ == completion::exception ||
               state_ == completion::stopped)
                return ep_;
            return {};
        }

        /// True when the coroutine was stopped via the stop token.
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
            frame allocator and, if stop has already been requested,
            throws the internal sentinel exception so the body never
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

        /** Awaiter wrapping a nested `co_await` of an @ref IoAwaitable.

            Forwards the environment to the inner awaitable's
            environment-taking `await_suspend` and restores the
            thread-local frame allocator before the body resumes. Unlike
            `task`'s, it also checks the stop token on resumption, throwing
            the internal sentinel so a stop request unwinds the body before
            it observes the I/O result.

            @tparam Awaitable The awaitable being transformed.
        */
        template<class Awaitable>
        struct transform_awaiter
        {
            std::decay_t<Awaitable> a_;
            promise_type* p_;

            bool await_ready() noexcept
            {
                return a_.await_ready();
            }

            // Check the stop token BEFORE the coroutine body
            // sees the result of the I/O operation.
            decltype(auto) await_resume()
            {
                set_current_frame_allocator(
                    p_->environment()->frame_allocator);
                if(p_->environment()->stop_token.stop_requested())
                    throw detail::stop_requested_exception{};
                return a_.await_resume();
            }

            template<class Promise>
            auto await_suspend(
                std::coroutine_handle<Promise> h) noexcept
            {
                using R = decltype(
                    a_.await_suspend(h, p_->environment()));
                if constexpr (std::is_same_v<
                    R, std::coroutine_handle<>>)
                    return detail::symmetric_transfer(
                        a_.await_suspend(h, p_->environment()));
                else
                    return a_.await_suspend(
                        h, p_->environment());
            }
        };

        /** Transform a nested awaitable before `co_await`.

            Wraps an @ref IoAwaitable in a @ref transform_awaiter so the
            coroutine's environment is propagated into it and the stop
            token is checked on resumption. A diagnostic is emitted if the
            awaitable does not satisfy @ref IoAwaitable.

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
                static_assert(sizeof(A) == 0,
                    "requires IoAwaitable");
            }
        }
    };

    /** Handle to the owned coroutine frame.

        Null when the quitter is empty (for example after a move or after
        @ref release). Prefer @ref handle to read this; the member is
        public for use by the coroutine machinery.
    */
    std::coroutine_handle<promise_type> h_;

    /// Destroy the quitter and its coroutine frame if owned.
    ~quitter()
    {
        if(h_)
            h_.destroy();
    }

    /// Return false; quitters are never immediately ready.
    bool await_ready() const noexcept
    {
        return false;
    }

    /** Return the result, rethrow exception, or propagate stop.

        When stopped, throws stop_requested_exception so that a
        parent quitter also stops.  A parent task<T> will see this
        as an unhandled exception — by design.
    */
    auto await_resume()
    {
        if(h_.promise().stopped())
            throw detail::stop_requested_exception{};
        if(h_.promise().state_ == promise_type::completion::exception)
            std::rethrow_exception(h_.promise().ep_);
        if constexpr (! std::is_void_v<T>)
            return std::move(*h_.promise().result_);
        else
            return;
    }

    /// Start execution with the caller's context.
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> cont,
        io_env const* env)
    {
        h_.promise().set_continuation(cont);
        h_.promise().set_environment(env);
        return h_;
    }

    /** Return the coroutine handle.

        @note Do not call `destroy()` on the returned handle while
        the quitter is being awaited. The quitter's lifetime is
        normally managed by `run_async`, `run`, or the awaiting
        parent; manually destroying a suspended quitter that another
        coroutine is awaiting produces undefined behavior. For
        cooperative cancellation, use `std::stop_token`.

        @return The coroutine handle.
    */
    std::coroutine_handle<promise_type> handle() const noexcept
    {
        return h_;
    }

    /** Release ownership of the coroutine frame.

        @note If the caller intends to call `destroy()` on the
        released handle, it must do so only when the quitter has not
        started or has fully completed. Destroying a suspended
        quitter that is being awaited produces undefined behavior.
    */
    void release() noexcept
    {
        h_ = nullptr;
    }

    quitter(quitter const&) = delete;
    quitter& operator=(quitter const&) = delete;

    /// Construct by moving, transferring ownership.
    quitter(quitter&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    /// Assign by moving, transferring ownership.
    quitter& operator=(quitter&& other) noexcept
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
    explicit quitter(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
};

} // namespace capy
} // namespace boost

#endif
