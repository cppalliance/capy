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
        promise_type() noexcept
            : state_(completion::running)
        {
        }

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

        quitter get_return_object()
        {
            return quitter{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

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

                void await_resume() const noexcept
                {
                }
            };
            return awaiter{this};
        }

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

    /// Return the coroutine handle.
    std::coroutine_handle<promise_type> handle() const noexcept
    {
        return h_;
    }

    /// Release ownership of the coroutine frame.
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
