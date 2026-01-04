//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_AFFINE_HPP
#define BOOST_CAPY_AFFINE_HPP

#include <boost/capy/detail/config.hpp>

#ifdef BOOST_CAPY_HAS_CORO

#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

/** Concept for types that can dispatch coroutine resumption.

    A dispatcher is a callable that accepts a coroutine handle
    and arranges for it to be resumed on the target execution
    context. Since std::coroutine_handle has operator() which
    calls resume(), the dispatcher can invoke the handle directly.

    @par Example
    @code
    struct my_dispatcher
    {
        void operator()(std::coroutine_handle<> h) const
        {
            // Queue h for execution on target context
            thread_pool_.post([h] { h(); });
        }
    };
    @endcode

    @tparam D The dispatcher type to check.
    @tparam P The promise type for the coroutine handle (default void).
*/
template<typename D, typename P = void>
concept dispatcher = requires(D d, std::coroutine_handle<P> h) { d(h); };

/** Concept for awaitables that support scheduler affinity.

    An affine_awaitable is an awaitable that accepts a dispatcher
    in its await_suspend method, enabling zero-overhead scheduler
    affinity. When an operation completes, it uses the dispatcher
    to resume the coroutine on the correct execution context.

    @par Requirements
    The type must provide `await_suspend(handle, dispatcher)`
    accepting a coroutine handle and a dispatcher reference.
    The dispatcher must satisfy the dispatcher concept.
    The other awaitable requirements (await_ready, await_resume)
    are enforced by the compiler when used in a co_await expression.

    @par Example
    @code
    struct affine_async_op
    {
        int result_;

        bool await_ready() const noexcept { return false; }

        template<typename Dispatcher>
        void await_suspend(std::coroutine_handle<> h, Dispatcher& d) const
        {
            // Start async work, then resume via dispatcher
            start_async([h, &d]() {
                d(h);
            });
        }

        int await_resume() const noexcept { return result_; }
    };
    @endcode

    @tparam A The awaitable type to check.
    @tparam D The dispatcher type.
    @tparam P The promise type for the coroutine handle (default void).
*/
template<typename A, typename D, typename P = void>
concept affine_awaitable =
    dispatcher<D, P> &&
    requires(A a, std::coroutine_handle<P> h, D& d) {
        a.await_suspend(h, d);
    };

/** Wrapper that bridges affine awaitables to standard coroutine machinery.

    This adapter wraps an affine_awaitable and provides the standard
    awaiter interface expected by the compiler. It captures a pointer
    to the dispatcher and forwards it to the awaitable's extended
    await_suspend method.

    @par Usage
    This is typically used in await_transform to adapt affine awaitables:
    @code
    template<typename Awaitable>
    auto await_transform(Awaitable&& a)
    {
        if constexpr (affine_awaitable<A, Dispatcher>) {
            return affine_awaiter{
                std::forward<Awaitable>(a), &dispatcher_};
        }
        // ... handle other cases
    }
    @endcode

    @par Dispatcher
    The dispatcher must satisfy the dispatcher concept, i.e.,
    be callable with a coroutine handle:
    @code
    struct Dispatcher
    {
        void operator()(std::coroutine_handle<> h);
    };
    @endcode

    @tparam Awaitable The affine awaitable type being wrapped.
    @tparam Dispatcher The dispatcher type for resumption.
*/
template<typename Awaitable, typename Dispatcher>
struct affine_awaiter {
    Awaitable awaitable_;
    Dispatcher* dispatcher_;

    bool await_ready() {
        return awaitable_.await_ready();
    }

    auto await_suspend(std::coroutine_handle<> h) {
        return awaitable_.await_suspend(h, *dispatcher_);
    }

    decltype(auto) await_resume() {
        return awaitable_.await_resume();
    }
};

template<typename A, typename D>
affine_awaiter(A&&, D*) -> affine_awaiter<A, D>;

/** Unified context serving as both dispatcher and scheduler.

    This class wraps a scheduler and provides a unified interface
    that works with both P2300 senders and traditional awaitables.
    It acts as a dispatcher (callable with coroutine handles) while
    also providing access to the underlying scheduler for sender
    operations like continues_on.

    @par Dispatcher Interface
    The class satisfies the dispatcher concept:
    @code
    resume_context<MyScheduler> ctx{scheduler};
    ctx(h);  // Dispatches coroutine handle via scheduler
    @endcode

    @par Scheduler Access
    For P2300 sender operations:
    @code
    auto sender = continues_on(some_sender, ctx.scheduler());
    @endcode

    @par Scheduler Requirements
    The scheduler type must provide a dispatch method:
    @code
    struct Scheduler
    {
        template<typename F>
        void dispatch(F&& f);
    };
    @endcode

    @tparam Scheduler The underlying scheduler type.
*/
template<typename Scheduler>
class resume_context {
    Scheduler* sched_;

public:
    /** Construct from a scheduler reference.

        @param s The scheduler to wrap. Must remain valid for the
            lifetime of this context.
    */
    explicit resume_context(Scheduler& s) noexcept
        : sched_(&s)
    {
    }

    resume_context(resume_context const&) = default;
    resume_context& operator=(resume_context const&) = default;

    /** Dispatch a continuation via the scheduler.

        @param f A nullary function object to dispatch.
    */
    template<typename F>
    void operator()(F&& f) const {
        sched_->dispatch(std::forward<F>(f));
    }

    /** Access the underlying scheduler.

        @return A reference to the wrapped scheduler.
    */
    Scheduler& scheduler() const noexcept {
        return *sched_;
    }

    bool operator==(resume_context const&) const noexcept = default;
};

/** CRTP mixin providing scheduler affinity for promise types.

    This mixin adds dispatcher storage and an affinity-aware
    final_suspend to promise types. When a dispatcher is set,
    the continuation is resumed through it; otherwise, direct
    symmetric transfer is used.

    @par Usage
    Inherit from this mixin using CRTP:
    @code
    struct promise_type
        : affine_promise<promise_type, my_dispatcher>
    {
        // Your promise implementation...
        // final_suspend() is provided by the mixin
    };
    @endcode

    @par Behavior
    - If a dispatcher is set, final_suspend dispatches the
      continuation through it before returning noop_coroutine
    - If no dispatcher is set, final_suspend performs direct
      symmetric transfer to the continuation
    - An optional done flag can be set to signal completion

    @par Dispatcher
    The dispatcher must satisfy the dispatcher concept, i.e.,
    be callable with a coroutine handle:
    @code
    struct Dispatcher
    {
        void operator()(std::coroutine_handle<> h);
    };
    @endcode

    @tparam Derived The derived promise type (CRTP).
    @tparam Dispatcher The dispatcher type for resumption.
*/
template<typename Derived, typename Dispatcher>
class affine_promise {
protected:
    std::coroutine_handle<> continuation_;
    std::optional<Dispatcher> dispatcher_;
    bool* done_flag_ = nullptr;

public:
    /** Set the continuation handle for symmetric transfer.

        @param h The coroutine handle to resume when this
            coroutine completes.
    */
    void set_continuation(std::coroutine_handle<> h) noexcept {
        continuation_ = h;
    }

    /** Set the dispatcher for affine resumption.

        @param d The dispatcher to use for resuming the
            continuation.
    */
    void set_dispatcher(Dispatcher d) {
        dispatcher_.emplace(std::move(d));
    }

    /** Set a flag to be marked true on completion.

        @param flag Reference to a bool that will be set to
            true when the coroutine reaches final_suspend.
    */
    void set_done_flag(bool& flag) noexcept {
        done_flag_ = &flag;
    }

    /** Return a final awaiter with affinity support.

        If a dispatcher is set, the continuation is resumed
        through it. Otherwise, direct symmetric transfer occurs.

        @return An awaiter for final suspension.
    */
    auto final_suspend() noexcept {
        struct final_awaiter {
            affine_promise* p_;

            bool await_ready() noexcept { return false; }

            std::coroutine_handle<>
            await_suspend(std::coroutine_handle<>) noexcept {
                if (p_->done_flag_)
                    *p_->done_flag_ = true;

                if (p_->dispatcher_) {
                    // Resume continuation via dispatcher
                    if (p_->continuation_)
                        (*p_->dispatcher_)(p_->continuation_);
                    return std::noop_coroutine();
                }
                // Direct symmetric transfer
                return p_->continuation_ ? p_->continuation_
                                         : std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };
        return final_awaiter{this};
    }
};

/** CRTP mixin providing awaitable interface for task types.

    This mixin makes a task type awaitable with support for both
    legacy coroutines (no dispatcher) and affine coroutines
    (with dispatcher). It provides both overloads of await_suspend.

    @par Requirements
    The derived class must provide:
    - `handle()` returning the coroutine_handle

    The promise type must provide:
    - `set_continuation(handle)` to store the caller
    - `set_dispatcher(dispatcher)` to store the dispatcher
    - `result()` to retrieve the coroutine result

    @par Usage
    @code
    template<typename T>
    class task
        : public affine_task<T, task<T>, my_dispatcher>
    {
        handle_type handle_;

    public:
        handle_type handle() const { return handle_; }
        // ...
    };
    @endcode

    @par Await Paths
    - Legacy: `co_await task` calls await_suspend(handle)
    - Affine: await_transform wraps in affine_awaiter which
      calls await_suspend(handle, dispatcher)

    @par Dispatcher
    The dispatcher must satisfy the dispatcher concept, i.e.,
    be callable with a coroutine handle:
    @code
    struct Dispatcher
    {
        void operator()(std::coroutine_handle<> h);
    };
    @endcode

    @tparam T The result type of the task.
    @tparam Derived The derived task type (CRTP).
    @tparam Dispatcher The dispatcher type for affine resumption.
*/
template<typename T, typename Derived, typename Dispatcher>
class affine_task {
    Derived& self() { return static_cast<Derived&>(*this); }
    Derived const& self() const { return static_cast<Derived const&>(*this); }

public:
    /** Check if the task has already completed.

        @return true if the coroutine is done.
    */
    bool await_ready() const noexcept {
        return self().handle().done();
    }

    /** Suspend and start the task (legacy path).

        This overload is used when no dispatcher is available.
        The continuation will be resumed via direct symmetric
        transfer when the task completes.

        @param caller The calling coroutine's handle.
        @return The task's coroutine handle to resume.
    */
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> caller) noexcept {
        self().handle().promise().set_continuation(caller);
        return self().handle();
    }

    /** Suspend and start the task (affine path).

        This overload is used when a dispatcher is available.
        The continuation will be resumed through the dispatcher
        when the task completes, ensuring scheduler affinity.

        @param caller The calling coroutine's handle.
        @param d The dispatcher for resuming the continuation.
        @return The task's coroutine handle to resume.
    */
    template<typename D>
        requires std::convertible_to<D, Dispatcher>
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> caller, D&& d) noexcept {
        self().handle().promise().set_dispatcher(std::forward<D>(d));
        self().handle().promise().set_continuation(caller);
        return self().handle();
    }

    /** Retrieve the task result.

        @return The value produced by the coroutine, or rethrows
            any captured exception.
    */
    decltype(auto) await_resume() {
        return self().handle().promise().result();
    }
};

namespace detail {

template<typename T>
auto get_awaitable(T&& expr) {
    if constexpr (requires { std::forward<T>(expr).operator co_await(); })
        return std::forward<T>(expr).operator co_await();
    else if constexpr (requires { operator co_await(std::forward<T>(expr)); })
        return operator co_await(std::forward<T>(expr));
    else
        return std::forward<T>(expr);
}

template<typename T>
using awaitable_type = decltype(get_awaitable(std::declval<T>()));

template<typename A>
using await_result_t = decltype(std::declval<awaitable_type<A>>().await_resume());

template<typename Dispatcher>
struct dispatch_awaitable {
    Dispatcher& dispatcher_;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) const {
        dispatcher_(h);
    }

    void await_resume() const noexcept {}
};

struct transfer_to_caller {
    std::coroutine_handle<> caller_;

    bool await_ready() noexcept { return false; }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<>) noexcept {
        return caller_;
    }

    void await_resume() noexcept {}
};

template<typename T>
class affinity_trampoline
{
public:
    struct promise_type {
        std::optional<T> value_;
        std::exception_ptr exception_;
        std::coroutine_handle<> caller_;

        affinity_trampoline get_return_object() {
            return affinity_trampoline{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        transfer_to_caller final_suspend() noexcept {
            return {caller_};
        }

        template<typename U>
        void return_value(U&& v) {
            value_.emplace(std::forward<U>(v));
        }

        void unhandled_exception() {
            exception_ = std::current_exception();
        }
    };

private:
    std::coroutine_handle<promise_type> handle_;

public:
    explicit affinity_trampoline(std::coroutine_handle<promise_type> h)
        : handle_(h)
    {
    }

    affinity_trampoline(affinity_trampoline&& o) noexcept
        : handle_(std::exchange(o.handle_, {}))
    {
    }

    ~affinity_trampoline() {
        if (handle_)
            handle_.destroy();
    }

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().caller_ = caller;
        return handle_;
    }

    T await_resume() {
        if (handle_.promise().exception_)
            std::rethrow_exception(handle_.promise().exception_);
        return std::move(*handle_.promise().value_);
    }
};

template<>
class affinity_trampoline<void>
{
public:
    struct promise_type {
        std::exception_ptr exception_;
        std::coroutine_handle<> caller_;

        affinity_trampoline get_return_object() {
            return affinity_trampoline{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        transfer_to_caller final_suspend() noexcept {
            return {caller_};
        }

        void return_void() noexcept {}

        void unhandled_exception() {
            exception_ = std::current_exception();
        }
    };

private:
    std::coroutine_handle<promise_type> handle_;

public:
    explicit affinity_trampoline(std::coroutine_handle<promise_type> h)
        : handle_(h)
    {
    }

    affinity_trampoline(affinity_trampoline&& o) noexcept
        : handle_(std::exchange(o.handle_, {}))
    {
    }

    ~affinity_trampoline() {
        if (handle_)
            handle_.destroy();
    }

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().caller_ = caller;
        return handle_;
    }

    void await_resume() {
        if (handle_.promise().exception_)
            std::rethrow_exception(handle_.promise().exception_);
    }
};

} // detail

/** Create an affinity trampoline for a legacy awaitable.

    This function wraps an awaitable in a trampoline coroutine
    that ensures resumption occurs via the specified dispatcher.
    After the inner awaitable completes, the trampoline dispatches
    the continuation to the dispatcher before transferring control
    back to the caller.

    This is the fallback path for awaitables that don't implement
    the affine_awaitable protocol. Prefer implementing the protocol
    for zero-overhead affinity.

    @par Usage
    Typically used in await_transform for legacy awaitables:
    @code
    template<typename Awaitable>
    auto await_transform(Awaitable&& a)
    {
        using A = std::remove_cvref_t<Awaitable>;

        if constexpr (affine_awaitable<A, Dispatcher>) {
            // Zero overhead path
            return affine_awaiter{
                std::forward<Awaitable>(a), &dispatcher_};
        } else {
            // Trampoline fallback
            return make_affine(
                std::forward<Awaitable>(a), dispatcher_);
        }
    }
    @endcode

    @par Dispatcher Requirements
    The dispatcher must satisfy the dispatcher concept:
    @code
    struct Dispatcher
    {
        void operator()(std::coroutine_handle<> h);
    };
    @endcode

    @param awaitable The awaitable to wrap.
    @param dispatcher A callable used to dispatch the continuation.
        Must remain valid until the awaitable completes.

    @return An awaitable that yields the same result as the wrapped
        awaitable, with resumption occurring via the dispatcher.
*/
template<typename Awaitable, typename Dispatcher>
auto make_affine(Awaitable&& awaitable, Dispatcher& dispatcher)
    -> detail::affinity_trampoline<detail::await_result_t<Awaitable>>
{
    using result_t = detail::await_result_t<Awaitable>;

    if constexpr (std::is_void_v<result_t>) {
        co_await detail::get_awaitable(std::forward<Awaitable>(awaitable));
        co_await detail::dispatch_awaitable<Dispatcher>{dispatcher};
    } else {
        auto result = co_await detail::get_awaitable(
            std::forward<Awaitable>(awaitable));
        co_await detail::dispatch_awaitable<Dispatcher>{dispatcher};
        co_return result;
    }
}

} // capy
} // boost

#endif

#endif
