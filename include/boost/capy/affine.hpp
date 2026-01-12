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

#include <boost/capy/coro.hpp>
#include <boost/capy/concept/dispatcher.hpp>
#include <boost/capy/concept/affine_awaitable.hpp>
#include <boost/capy/concept/stoppable_awaitable.hpp>

#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

/** A type-erased wrapper for dispatcher objects.

    This class provides type erasure for any type satisfying the `dispatcher`
    concept, enabling runtime polymorphism without virtual functions. It stores
    a pointer to the original dispatcher and a function pointer to invoke it,
    allowing dispatchers of different types to be stored uniformly.

    @par Thread Safety
    The `any_dispatcher` itself is not thread-safe for concurrent modification,
    but `operator()` is const and safe to call concurrently if the underlying
    dispatcher supports concurrent dispatch.

    @par Lifetime
    The `any_dispatcher` stores a pointer to the original dispatcher object.
    The caller must ensure the referenced dispatcher outlives the `any_dispatcher`
    instance. This is typically satisfied when the dispatcher is an executor
    stored in a coroutine promise or service provider.

    @par Example
    @code
    void store_dispatcher(any_dispatcher d)
    {
        // Can store any dispatcher type uniformly
        auto h = d(some_coroutine);  // Invoke through type-erased interface
    }

    executor_base const& ex = get_executor();
    store_dispatcher(ex);  // Implicitly converts to any_dispatcher
    @endcode

    @see dispatcher
    @see executor_base
*/
class any_dispatcher
{
    void const* d_ = nullptr;
    coro(*f_)(void const*, coro) = nullptr;

public:
    /** Default constructor.

        Constructs an empty `any_dispatcher`. Calling `operator()` on a
        default-constructed instance results in undefined behavior.
    */
    any_dispatcher() = default;

    /** Copy constructor.

        Copies the internal pointer and function, preserving identity.
        This enables the same-dispatcher optimization when passing
        any_dispatcher through coroutine chains.
    */
    any_dispatcher(any_dispatcher const&) = default;

    /** Copy assignment operator. */
    any_dispatcher& operator=(any_dispatcher const&) = default;

    /** Constructs from any dispatcher type.

        Captures a reference to the given dispatcher and stores a type-erased
        invocation function. The dispatcher must remain valid for the lifetime
        of this `any_dispatcher` instance.

        @param d The dispatcher to wrap. Must satisfy the `dispatcher` concept.
                 A pointer to this object is stored internally; the dispatcher
                 must outlive this wrapper.
    */
    template<dispatcher D>
        requires (!std::same_as<std::decay_t<D>, any_dispatcher>)
    any_dispatcher(D const& d)
        : d_(&d)
        , f_([](void const* pd, coro h) {
                return static_cast<D const*>(pd)->operator()(h);
            })
    {
    }

    /** Returns true if this instance holds a valid dispatcher.

        @return `true` if constructed with a dispatcher, `false` if
                default-constructed.
    */
    explicit operator bool() const noexcept
    {
        return d_ != nullptr;
    }

    /** Compares two dispatchers for identity.

        Two `any_dispatcher` instances are equal if they wrap the same
        underlying dispatcher object (pointer equality). This enables
        the affinity optimization: when `caller_dispatcher == my_dispatcher`,
        symmetric transfer can proceed without a `running_in_this_thread()`
        check.

        @param other The dispatcher to compare against.

        @return `true` if both wrap the same dispatcher object.
    */
    bool operator==(any_dispatcher const& other) const noexcept
    {
        return d_ == other.d_;
    }

    /** Dispatches a coroutine handle through the wrapped dispatcher.

        Invokes the stored dispatcher with the given coroutine handle,
        returning a handle suitable for symmetric transfer.

        @param h The coroutine handle to dispatch for resumption.

        @return A coroutine handle that the caller may use for symmetric
                transfer, or `std::noop_coroutine()` if the dispatcher
                posted the work for later execution.

        @pre This instance was constructed with a valid dispatcher
             (not default-constructed).
    */
    coro operator()(coro h) const
    {
        return f_(d_, h);
    }
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

} // capy
} // boost

#endif
