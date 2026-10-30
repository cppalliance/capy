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
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/context_awaitable.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/context_awaitable_promise_base.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/frame_alloc_mixin.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/detail/quitter_stopped.hpp>
#include <boost/capy/results.hpp>

#include <coroutine>
#include <exception>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>

/* Stop-aware coroutine task.

   quitter<T> is task<T>, except that a stop request ends its frames
   by abandonment instead of resuming them. Nothing in the body runs
   after the stop, so nothing can intercept it, and no exception is
   thrown inside the quitter.

   Segment. A quitter awaited by anything other than a quitter opens a
   segment in its await_suspend. The segment is one coroutine frame,
   quitter_segment, that is both the segment's owner (its chain_owner is
   the await_context::owner every frame in the segment sees) and the
   gate. A quitter awaited by a quitter joins the parent's segment. A
   quitter started directly by a launcher (run_async, run(ex)) runs in
   the launcher's segment; it still gets a gate, and the launcher's
   chain_owner is the one the gate abandons. The launcher creates the
   gate with launch_handle() and resumes the gate, not the quitter, so
   the gate checks the stop before the body starts. A launcher that
   resumes the quitter's own handle instead gets a gate when the body
   starts, with no such check.

   Gate. A quitter frame never hands its own handle to an operation. Its
   transform_awaiter records the frame as the gate's target and passes
   the gate's handle instead, so an operation completes into the gate.
   The gate then resumes the target, or, if a stop was requested,
   abandons the segment with chain_owner::abandon_for_stop. The
   operation has finished with the frame by then (the corosio rule: no
   frame is destroyed while an operation holds its continuation).

   Synchronous completions. The stop is checked before the
   inner await_ready, so a pending stop starts no work. An inner
   awaitable that completes without going through the gate (ready,
   await_suspend returning false or the gate's own handle) always has
   its await_resume called, so anything it acquired is handed over; the
   stop takes effect at the next suspension. The exception is a
   results<> holding cond::canceled while our stop is pending: the
   inner awaitable saw the stop first and acquired nothing (async_mutex,
   async_event and async_waker all answer a stop that lands between our
   check and theirs this way), so the segment is abandoned there. That
   result is taken early and kept in the awaiter, because await_resume
   runs in the resumed body, too late to abandon.

   Unwind. When the segment's owner is resumed after an abandon, it
   releases the opening quitter's handle and destroys the top frame,
   which destroys the frames below it. After a stop it then resumes the
   parent, whose co_await yields the stopped value (stopped_value<T>).
   After a teardown it abandons the parent, as run's boundary_trampoline
   does. A segment frame destroyed instead of resumed (posted to a
   closed executor) unwinds as teardown from its destructor.

   Why the gate is never resumed while it runs. Its body is
   `for(;;) co_await hop{}`, and every decision is made in
   hop::await_suspend, after the frame has suspended. Its handle is
   reachable only from the one operation a quitter frame of the segment
   is waiting on, and from the owner's post, which happens only when no
   operation holds it. Once await_suspend has handed the handle on, by
   returning it, resuming a target inline or posting, it touches nothing
   of the frame again, because another thread may already be running or
   destroying it. */

namespace boost {
namespace capy {

template<typename T>
struct quitter;

namespace detail {

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

/* What a stopped quitter<T> delivers to its parent.

   results<Ts...>: canceled, with each payload value-initialized.
   std::error_code: canceled.
   Any other T, void included: quitter_stopped, a std::system_error
   with canceled, thrown at the boundary. A void return would read as
   success. */
template<class T>
struct stopped_value
{
    static T get()
    {
#if defined(__cpp_exceptions)
        throw quitter_stopped();
#else
        // sizeof(T*): T may be void.
        static_assert(sizeof(T*) == 0,
            "a stopped quitter<T> for a T other than results or "
            "std::error_code reports the stop by throwing, "
            "which needs exceptions");
#endif
    }
};

template<class... Ts>
struct stopped_value<results<Ts...>>
{
    static_assert(
        (std::is_default_constructible_v<Ts> && ...),
        "a stopped quitter<results<Ts...>> value-initializes every Ts");

    static results<Ts...> get()
    {
        return results<Ts...>(make_error_code(error::canceled), Ts{}...);
    }
};

template<>
struct stopped_value<std::error_code>
{
    static std::error_code get() noexcept
    {
        return make_error_code(error::canceled);
    }
};

template<class T>
struct is_quitter : std::false_type {};

template<class T>
struct is_quitter<quitter<T>> : std::true_type {};

// A stop that can be carried out: requested, with an owner to abandon.
// With no owner a quitter cannot stop, and runs on as a task would; a
// launcher that runs quitters must bind a chain_owner.
inline bool
quitter_stop_pending(await_context const* env) noexcept
{
    if(!env->stop_token.stop_requested())
        return false;
    BOOST_CAPY_ASSERT(env->owner &&
        "a quitter's environment needs an owner to stop");
    return env->owner != nullptr;
}

struct quitter_opener;

// One per segment: the owner and the gate in one coroutine frame.
struct quitter_segment
{
    struct promise_type;

    std::coroutine_handle<promise_type> h_;
};

// The part of a quitter that opened a segment, without its T.
struct quitter_opener
{
    // Takes the quitter's frame handle, leaving the quitter empty.
    std::coroutine_handle<> (*release_top_)(quitter_opener&) noexcept =
        nullptr;
    quitter_segment seg_{};
    bool stopped_ = false;
    // Set while a parent is suspended awaiting the quitter.
    bool lent_ = false;
};

// Not the chain's frame allocator: after a teardown this frame is freed
// once the parent has been abandoned, when that allocator may be gone.
// run's boundary_trampoline does the same.
struct quitter_segment::promise_type
    : process_frame_mixin
{
    // The segment's owner, when a quitter opened the segment.
    chain_owner owner_;
    // The environment of the segment's frames, when a quitter opened it.
    await_context env_;
    // The environment the frames run in: env_, or the launcher's.
    await_context const* frame_env_ = nullptr;
    // The awaiting coroutine, and its own owner for a teardown.
    continuation parent_;
    // The quitter frame waiting in the gate.
    std::coroutine_handle<> target_;
    // The quitter that opened the segment; null for a gate only.
    quitter_opener* opener_ = nullptr;
    // An unwind is still owed. Cleared when the top frame completes,
    // when the unwind runs, and by the quitter when it destroys this.
    bool active_ = false;

    ~promise_type()
    {
        // Destroyed instead of resumed: a closed executor discarded the
        // owner's post. That is teardown, whatever the cause.
        if(active_)
        {
            active_ = false;
            // This frame is going away; the quitter must not destroy it
            // again when the parent's unwind reaches it.
            opener_->seg_.h_ = nullptr;
            opener_->release_top_(*opener_).destroy();
            auto parent = parent_;
            capy::abandon(parent);
        }
    }

    quitter_segment get_return_object() noexcept
    {
        return quitter_segment{
            std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; } // LCOV_EXCL_LINE the body never ends
    void return_void() noexcept {} // LCOV_EXCL_LINE the body never ends
    void unhandled_exception() noexcept { std::terminate(); } // LCOV_EXCL_LINE the body never throws

    std::coroutine_handle<> handle() noexcept
    {
        return std::coroutine_handle<promise_type>::from_promise(*this);
    }

    // Abandon the segment for a stop. The caller holds the waiting
    // frame and touches nothing of it afterwards.
    void stop() noexcept
    {
        frame_env_->owner->abandon_for_stop();
    }

    std::coroutine_handle<> decide() noexcept
    {
        if(opener_ && owner_.abandoned())
        {
            active_ = false;
            auto* op = opener_;
            op->release_top_(*op).destroy();
            if(owner_.stopped())
            {
                op->stopped_ = true;
                return parent_.h;
            }
            auto parent = parent_;
            capy::abandon(parent);
            return std::noop_coroutine();
        }
        if(quitter_stop_pending(frame_env_))
        {
            stop();
            return std::noop_coroutine();
        }
        return target_;
    }
};

struct quitter_hop
{
    bool await_ready() const noexcept { return false; }

    auto await_suspend(
        std::coroutine_handle<quitter_segment::promise_type> h) noexcept
    {
        return detail::symmetric_transfer(h.promise().decide());
    }

    void await_resume() const noexcept {}
};

inline quitter_segment
make_quitter_segment()
{
    for(;;)
        co_await quitter_hop{};
}

} // namespace detail

/** Defers a coroutine body until awaited, and ends it on a stop request without resuming it.

    A `quitter` is a @ref task whose author has chosen that a stop
    request ends it. When the stop token fires, the quitter's frames are
    not resumed: they are destroyed, from the top down, as an abandoned
    chain is (see @ref abandon). Only destructors run. Nothing in the
    body can observe or intercept the stop, not even `catch(...)`, and no
    exception is thrown inside the quitter. The stop is reported to the
    first coroutine above that is not a quitter, as a value: see
    Await-returns.

    A quitter stops only quitter frames. A @ref task below a quitter
    receives the stop request as usual, and returns through its own code
    before the quitter is destroyed. A task above a quitter receives the
    stopped value from `co_await`. A quitter awaited by a quitter is
    stopped together with it.

    Everything else (frame allocation, environment propagation,
    symmetric transfer, move semantics) is as for @ref task.

    @par Await-effects

    Let `q` be a `quitter<T>`. If the stop token in the awaiting
    environment has already been triggered, `co_await q` does not start
    the body, and await-returns the stopped value. Otherwise it suspends
    the awaiting coroutine and transfers control directly into the
    quitter's body on the current thread; no executor operation is
    posted. The quitter propagates the caller's executor, stop token and
    frame allocator to every `co_await` inside the body.

    At every `co_await` in the body, the stop token is checked before
    the awaited operation is started. An operation that completes
    asynchronously does not resume the body directly: the stop token is
    checked again first. If either check finds a stop, the body is not
    resumed. The quitter's frames are destroyed on the executor, and
    the coroutine that awaited the quitter is resumed with the stopped
    value. When `run_async` started the quitter, it reports the stop to
    its handlers instead, as Await-returns describes.

    An operation that completes without suspending always has
    its `await_resume` called, because its `await_ready` or
    `await_suspend` may already have acquired a resource, such as
    `async_mutex`'s lock, that only its `await_resume` hands over. If
    that result is a @ref results holding `cond::canceled` while a stop
    is pending, the operation saw the stop first and acquired nothing,
    so the quitter is stopped at that `co_await`. Any other result is
    handed to the body, and the stop takes effect at the next
    suspension. An operation whose result is not a @ref results can
    therefore hand the body one result it produced after seeing the
    stop.

    When the body returns or exits via an exception, control transfers
    directly back to the awaiting coroutine.

    @par Await-returns

    The value the body passed to `co_return`, moved out of the quitter,
    or nothing when `T` is `void`. If the body exits via an unhandled
    exception, that exception is rethrown instead.

    If the quitter was stopped, the stopped value instead:

    @li for `results<Ts...>`: `make_error_code(error::canceled)` with
        each `Ts` value-initialized; it compares equal to
        `cond::canceled`. Every `Ts` must be default-constructible.
    @li for `std::error_code`: `make_error_code(error::canceled)`.
    @li for any other `T`, `void` included: no value; an exception of
        an unspecified type derived from `std::system_error`, whose
        `code()` compares equal to `cond::canceled`, is thrown. A `void`
        return would read as success. Such a `quitter<T>` cannot be used
        where exceptions are disabled: use `quitter<results<>>` there.

    `co_await run(ex)(q)`, and the other @ref run overloads, yield the
    same stopped value. A quitter started by `run_async` reports a stop
    the same way. For `results<Ts...>` and `std::error_code`, the value
    handler receives the stopped value. For any other `T`, the error
    handler receives the exception. With no error handler, no handler
    is called and the stop is discarded. This also holds when the
    exception escapes a @ref task that awaited the quitter: the task is
    unwound, and with no error handler `run_async` discards the stop.
    Any other exception, a `std::system_error` with `cond::canceled`
    included, still calls `std::terminate`. That includes a
    `std::system_error` copy of the stop that a task throws.

    @par Await-postcondition
    The quitter's coroutine has completed, has been destroyed by a
    stop, or, when the stop was requested before it was awaited, was
    never started. A quitter must not be awaited twice.

    @par Remarks
    Supports _ContextAwaitable cancellation_. A quitter stops through the
    owner of its segment (@ref await_context::owner). A launcher that
    starts a quitter in an environment with no owner cannot stop it: a
    debug build asserts, and otherwise the quitter runs on as a
    @ref task would. A stop that arrives while
    the execution context is being shut down may end the quitter as a
    teardown instead, with nothing delivered. Stop the work, wait for it
    to finish, and only then destroy the context.

    A stop destroys quitter frames that are suspended at a `co_await`.
    GCC 15.2, and GCC 16 before 2026-07-02, at `-O0`, miscompile such a
    destruction when the `co_await` initializes a structured binding
    with an element that is not trivially destructible: the destructor
    of the never-constructed binding runs (GCC PR c++/124584). Bind the
    result to a named variable, and destructure it after the
    `co_await`:

    @code
    auto r = co_await mtx.scoped_lock();
    auto& [ec, guard] = r;
    @endcode

    @tparam T The result type. Use `quitter<>` for `quitter<void>`.

    @see @ref task, @ref ContextRunnable, @ref ContextAwaitable,
    @ref chain_owner
*/
template<typename T = void>
struct [[nodiscard]] BOOST_CAPY_CORO_AWAIT_ELIDABLE
    quitter
    : private detail::quitter_opener
{
    /** Stores `quitter<T>`'s result and routes its awaits through the segment's gate.

        This is the promise object the compiler associates with a
        `quitter<T>` coroutine. It satisfies the coroutine promise
        requirements and participates in the I/O awaitable protocol via
        @ref context_awaitable_promise_base. Unlike @ref task::promise_type,
        its `transform_awaitable` checks the stop token before each
        awaited operation starts. It hands the operation the segment's
        gate as the continuation, not this coroutine. It is part of
        the coroutine machinery and is not intended to be used directly
        by callers.

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

        template<class>
        friend struct quitter;

        std::exception_ptr ep_;

        // The gate of the segment this frame runs in.
        detail::quitter_segment::promise_type* seg_ = nullptr;

        // The gate this frame owns, when a launcher started it.
        detail::quitter_segment own_gate_{};

        // This frame opened its segment.
        bool top_ = false;

        void open_gate()
        {
            own_gate_ = detail::make_quitter_segment();
            seg_ = &own_gate_.h_.promise();
            seg_->frame_env_ = this->environment();
        }

    public:
        /// Construct the promise.
        promise_type() noexcept = default;

        /// Destroy the promise, and the gate it owns, if any.
        ~promise_type()
        {
            if(own_gate_.h_)
                own_gate_.h_.destroy();
        }

        /** Return the exception the body exited with, if any.

            @return The stored exception if the coroutine exited via an
            exception, otherwise a null `std::exception_ptr`.
        */
        std::exception_ptr exception() const noexcept
        {
            return ep_;
        }

        /** Return what a stopped `quitter<T>` delivers.

            Launchers that start a quitter directly call this when its
            segment was abandoned for a stop: see
            @ref chain_owner::stopped. Its result, or the exception it
            throws, is reported in place of the coroutine's own result.

            @return `make_error_code(error::canceled)` with value-initialized
            payloads for `results<Ts...>`, or
            `make_error_code(error::canceled)` for `std::error_code`.

            @throws std::system_error For any other `T`, `void` included,
            with a `code()` that compares equal to `cond::canceled`.
        */
        static T stopped_result()
        {
            return detail::stopped_value<T>::get();
        }

        /** Return the handle a launcher resumes to start the quitter.

            Launchers that start a quitter directly resume this handle,
            not the coroutine's own, so the stop is checked when the body
            would start rather than when the launcher was called. The
            handle is the quitter's gate, which either starts the body
            or, if a stop was requested by then, abandons the segment for
            the stop: see @ref chain_owner::abandon_for_stop.

            @par Preconditions
            The coroutine has not started, and its environment is set.

            @return The handle of the quitter's gate.

            @throws std::bad_alloc If the gate cannot be allocated.
        */
        std::coroutine_handle<> launch_handle()
        {
            open_gate();
            seg_->target_ =
                std::coroutine_handle<promise_type>::from_promise(*this);
            return seg_->handle();
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
            so the body does not start until the quitter is awaited or
            started. When the body is resumed, the awaiter restores the
            thread-local frame allocator. A quitter started directly by a
            launcher that did not call @ref launch_handle creates its
            gate at that point.

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

                // Allocates the gate of a quitter that a launcher started
                // without launch_handle(); a failure is reported as the
                // body's exception.
                void await_resume() const
                {
                    set_current_frame_allocator(
                        p_->environment()->frame_allocator);
                    if(!p_->seg_)
                        p_->open_gate();
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
                    // The top frame completed: its segment owes no
                    // unwind.
                    if(p_->top_)
                        p_->seg_->active_ = false;
                    return p_->continuation();
                }

                void await_resume() const noexcept {} // LCOV_EXCL_LINE final_suspend awaiter, never resumed
            };
            return awaiter{this};
        }

        /** Capture the in-flight exception from the coroutine body.

            Called by the compiler when the coroutine body exits via an
            unhandled exception. The exception is rethrown when the
            quitter is awaited, or routed to the error handler when it is
            run.
        */
        void unhandled_exception() noexcept
        {
            ep_ = std::current_exception();
        }

        /** Awaiter wrapping a nested `co_await` of an @ref ContextAwaitable.

            Forwards the environment to the inner awaitable's
            environment-taking `await_suspend`, and restores the
            thread-local frame allocator before the body resumes. Unlike
            `task`'s, it checks the stop token before the inner awaitable
            is touched. It passes the inner awaitable the segment's gate
            as the continuation, not this coroutine.

            @tparam Awaitable The awaitable being transformed.
        */
        template<class Awaitable>
        struct transform_awaiter
        {
            /// The wrapped awaitable, decayed and stored by value.
            std::decay_t<Awaitable> a_;

            /// The promise of the coroutine performing the `co_await`.
            promise_type* p_;

            /// The wrapped awaitable's await-result type.
            using result_type = decltype(
                std::declval<std::decay_t<Awaitable>&>().await_resume());

            /// True when the await-result is a @ref results, which is
            /// checked for a canceled stop after a synchronous completion.
            static constexpr bool checks_canceled =
                detail::is_results_v<result_type>;

            /// True when the checks that may call the wrapped awaitable's
            /// `await_resume` early cannot throw.
            static constexpr bool checks_noexcept =
                !checks_canceled || noexcept(
                    std::declval<std::decay_t<Awaitable>&>().await_resume());

            /// Empty stand-in for @ref result_ when nothing is checked.
            struct no_result {};

            /// The result of a synchronous completion, taken early by
            /// @ref stops_on_sync_completion and handed over by
            /// @ref await_resume.
            std::conditional_t<checks_canceled,
                std::optional<result_type>, no_result> result_{};

            /// True when the stop takes effect at this `co_await` without
            /// suspending: a stop was pending before the wrapped awaitable
            /// was touched, or it completed synchronously with a result
            /// canceled by that stop.
            bool stop_here_ = false;

            /** Take the result of a synchronous completion, and report
                whether the stop takes effect here instead.

                The wrapped awaitable's `await_resume` is always called,
                because its `await_ready` or `await_suspend` may already
                have acquired something that only `await_resume` hands
                over. A @ref results holding `cond::canceled` while our
                stop is pending saw the stop first and acquired nothing,
                so the quitter can be abandoned. Any other result is kept
                for @ref await_resume. A result that is not a
                @ref results is not taken early.

                @return `true` if the quitter is abandoned at this
                `co_await`.
            */
            bool stops_on_sync_completion() noexcept(checks_noexcept)
            {
                if constexpr (checks_canceled)
                {
                    result_.emplace(a_.await_resume());
                    // The stop check first: comparing with
                    // cond::canceled goes through the error category's
                    // virtual calls, and a stop is rarely pending.
                    return detail::quitter_stop_pending(p_->environment()) &&
                        std::get<0>(*result_) == cond::canceled;
                }
                else
                {
                    return false;
                }
            }

            /** Report whether the `co_await` can continue without
                suspending.

                The stop token is checked first. If a stop is pending, the
                wrapped awaitable is never touched: this returns `false`,
                and @ref await_suspend ends the quitter. Otherwise the
                wrapped awaitable's own `await_ready` decides. A `true`
                result means it completed synchronously. Its
                `await_resume` then hands over its result, even if a stop
                arrives afterwards, unless that result is a @ref results
                canceled by the stop (see @ref stops_on_sync_completion).

                Where `BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND` is on,
                this always returns `false`, and @ref await_suspend does both
                steps instead. See `detail::await_ready_then_suspend`.

                @return `true` if no suspension is needed.
            */
            bool await_ready() noexcept(checks_noexcept)
            {
#if BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND
                return false;
#else
                if(detail::quitter_stop_pending(p_->environment()))
                {
                    stop_here_ = true;
                    return false;
                }
                if(!a_.await_ready())
                    return false;
                stop_here_ = stops_on_sync_completion();
                return !stop_here_;
#endif
            }

            /** Restore the frame allocator, then return the wrapped
                awaitable's result.

                The body is resumed only when the wrapped awaitable
                completed synchronously, or completed through the gate
                with no stop requested.

                @return The wrapped awaitable's await-result, forwarded
                unchanged, or the result already taken from it.
            */
            decltype(auto) await_resume()
            {
                set_current_frame_allocator(
                    p_->environment()->frame_allocator);
                if constexpr (checks_canceled)
                {
                    if(result_)
                        return result_type(std::move(*result_));
                }
                return a_.await_resume();
            }

            /** Start the wrapped awaitable with the gate as its
                continuation, or end the quitter on a pending stop.

                If a stop is pending, the segment is abandoned for the
                stop and this coroutine stays suspended until it is
                destroyed. Otherwise this coroutine becomes the gate's
                target, and the wrapped awaitable's @ref ContextAwaitable
                overload is called with the gate's handle and the
                promise's environment. If that call completes without
                suspending (returning `false`, or the gate's own handle),
                this coroutine continues to @ref await_resume without
                passing through the gate, unless its result is a
                @ref results canceled by the stop (see
                @ref stops_on_sync_completion).

                @param h The coroutine performing the `co_await`.

                @return Where the workaround is off, the handle to resume:
                `h` to continue, a handle the wrapped awaitable returned,
                or `std::noop_coroutine()`. Where
                `BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND` is on, `false`
                to continue and `true` to stay suspended; the stop check
                and the wrapped awaitable's `await_ready` both run here,
                through `detail::await_ready_then_suspend`.
            */
            template<class Promise>
            auto await_suspend(
                std::coroutine_handle<Promise> h) noexcept(checks_noexcept)
            {
                // After the wrapped awaitable's await_suspend, the gate
                // may already have run and destroyed this frame on
                // another thread: only locals are used from then on,
                // except after a synchronous completion, which leaves
                // the frame to us.
                auto* seg = p_->seg_;
                auto const* env = p_->environment();
#if BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND
                if(detail::quitter_stop_pending(env))
                {
                    seg->stop();
                    return true;
                }
                seg->target_ = h;
                if(detail::await_ready_then_suspend(
                    a_, seg->handle(), env))
                    return true;
                if(stops_on_sync_completion())
                {
                    seg->stop();
                    return true;
                }
                return false;
#else
                if(stop_here_)
                {
                    seg->stop();
                    return std::coroutine_handle<>(std::noop_coroutine());
                }
                seg->target_ = h;
                std::coroutine_handle<> gate = seg->handle();
                using R = decltype(a_.await_suspend(gate, env));
                if constexpr (std::is_same_v<R, bool>)
                {
                    if(!a_.await_suspend(gate, env))
                        return continue_or_stop(h);
                    return std::coroutine_handle<>(std::noop_coroutine());
                }
                else if constexpr (std::is_void_v<R>)
                {
                    a_.await_suspend(gate, env);
                    return std::coroutine_handle<>(std::noop_coroutine());
                }
                else
                {
                    std::coroutine_handle<> r = a_.await_suspend(gate, env);
                    if(r == gate)
                        return continue_or_stop(h);
                    return r;
                }
#endif
            }

#if !BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND
            /** After a synchronous completion in @ref await_suspend,
                continue to @ref await_resume, or end the quitter.

                @param h The coroutine performing the `co_await`.

                @return `h`, or `std::noop_coroutine()` when the quitter is
                abandoned here (see @ref stops_on_sync_completion).
            */
            std::coroutine_handle<>
            continue_or_stop(std::coroutine_handle<> h)
                noexcept(checks_noexcept)
            {
                if(!stops_on_sync_completion())
                    return h;
                p_->seg_->stop();
                return std::noop_coroutine();
            }
#endif
        };

        /** Awaiter for a nested `co_await` of a quitter.

            The awaited quitter joins this coroutine's segment instead of
            opening one: it runs in the same environment and gate, and a
            stop ends both together. Its completion transfers straight
            back to this coroutine.

            @tparam Q The awaited quitter type.
        */
        template<class Q>
        struct join_awaiter
        {
            /// The awaited quitter, stored by value.
            Q q_;

            /// The promise of the coroutine performing the `co_await`.
            promise_type* p_;

            /// Return `false`; the joined quitter has not started.
            bool await_ready() const noexcept
            {
                return false;
            }

            /** Start the joined quitter, or end the segment on a pending
                stop.

                @param h The coroutine performing the `co_await`.

                @return The joined quitter's handle, for symmetric
                transfer, or `std::noop_coroutine()` after a stop. Where
                the workaround is on, `void`.
            */
            template<class Promise>
            auto await_suspend(std::coroutine_handle<Promise> h) noexcept
            {
                auto const* env = p_->environment();
                if(detail::quitter_stop_pending(env))
                {
                    p_->seg_->stop();
                    return detail::symmetric_transfer(
                        std::noop_coroutine());
                }
                return detail::symmetric_transfer(
                    q_.join(h, p_->seg_, env));
            }

            /** Restore the frame allocator, then return the joined
                quitter's result.

                @return The joined quitter's await-result.
            */
            decltype(auto) await_resume()
            {
                set_current_frame_allocator(
                    p_->environment()->frame_allocator);
                return q_.await_resume();
            }
        };

        /** Transform a nested awaitable before `co_await`.

            A quitter is wrapped in a @ref join_awaiter. Any other
            @ref ContextAwaitable is wrapped in a @ref transform_awaiter.
            A diagnostic is emitted for anything else.

            @param a The awaitable expression from `co_await a`.

            @return The awaiter wrapping `a`.
        */
        template<class Awaitable>
        auto transform_awaitable(Awaitable&& a)
        {
            using A = std::decay_t<Awaitable>;
            if constexpr (detail::is_quitter<A>::value)
            {
                return join_awaiter<A>{std::forward<Awaitable>(a), this};
            }
            else if constexpr (ContextAwaitable<A>)
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

        Null when the quitter is empty (for example after a move, after
        @ref release, or once a stop has destroyed the frame). Prefer
        @ref handle to read this; the member is public for use by the
        coroutine machinery.
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

    /** Return the result, rethrow the body's exception, or deliver the stopped value.

        @return The result value for non-void `T`, moved out of the
        quitter, or the stopped value if the quitter was stopped.

        @par Exception Safety
        If the body exited via an exception, that exception is rethrown.
        If the quitter was stopped and `T` is not `results<Ts...>` or
        `std::error_code`, a `std::system_error` whose `code()`
        compares equal to `cond::canceled` is thrown.
    */
    T await_resume()
    {
        lent_ = false;
        if(stopped_)
            return promise_type::stopped_result();
        auto& p = h_.promise();
        if(p.ep_)
            std::rethrow_exception(p.ep_);
        if constexpr (! std::is_void_v<T>)
            return std::move(*p.result_);
    }

    /** Open a segment and start the quitter with the caller's context.

        If the stop token in `env` has already been triggered, the body
        is not started. `cont` is returned, so the caller resumes at once
        with the stopped value.

        Otherwise the quitter opens a segment of its own. The segment's
        owner destroys the quitter's frames if the segment is abandoned.
        Its gate is what the quitter's awaits complete through. The body
        runs with `env`'s executor, stop token and frame allocator, and
        the segment's owner. Control then transfers into the body.

        @param cont The awaiting coroutine to resume when the quitter
        completes or is stopped.

        @param env The execution environment. It must outlive the
        quitter.

        @return The quitter's coroutine handle for symmetric transfer, or
        `cont`.

        @throws std::bad_alloc If the segment's frame cannot be
        allocated. When the awaiting coroutine is a @ref task, its
        awaiter's `await_suspend` is `noexcept`, so `std::terminate` is
        called instead.
    */
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> cont,
        await_context const* env)
    {
        if(env->stop_token.stop_requested())
        {
            stopped_ = true;
            return cont;
        }
        seg_ = detail::make_quitter_segment();
        auto& sp = seg_.h_.promise();
        sp.owner_.bind(seg_.h_, env->executor);
        sp.env_ = *env;
        sp.env_.owner = &sp.owner_;
        sp.frame_env_ = &sp.env_;
        sp.parent_.h = cont;
        sp.parent_.owner = env->owner;
        sp.opener_ = this;
        sp.active_ = true;
        auto& p = h_.promise();
        p.seg_ = &sp;
        p.top_ = true;
        p.set_environment(&sp.env_);
        p.set_continuation(cont);
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
        or the awaiting parent.

        @note The operation, queue or primitive holding a suspended
        coroutine gives it up by passing the continuation it keeps to
        @ref abandon. `handle()` belongs to the owning quitter object.

        @note To run the quitter for an awaiting coroutine, call its
        `await_suspend`, which opens a segment for it. Do not resume
        this handle with the awaiting coroutine's environment: a stop
        then abandons the awaiting coroutine's segment, and a @ref task
        chain in it is torn down with no handler called. When
        `run_async` or `run` owns that segment, a debug build asserts.
        A launcher that resumes the handle itself must own the segment:
        see @ref await_context::owner.

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

        @par Preconditions
        `other` is not being awaited.

        @par Postconditions
        `other` is empty and must not be awaited.

        @param other The quitter to move from.
    */
    quitter(quitter&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
        release_top_ = other.release_top_;
    }

    /** Assign by moving, transferring ownership.

        If this quitter already owns a coroutine frame, that frame is
        destroyed first. Self-assignment is a no-op.

        @par Preconditions
        The frame being replaced is not lent: no parent is suspended
        awaiting it. Replacing a lent frame calls `std::terminate`.
        `other` is not being awaited.

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
            stopped_ = false;
        }
        return *this;
    }

private:
    template<class>
    friend struct quitter;

    explicit quitter(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
        release_top_ = &release_top;
    }

    static std::coroutine_handle<>
    release_top(detail::quitter_opener& o) noexcept
    {
        return std::exchange(static_cast<quitter&>(o).h_, nullptr);
    }

    // Start as a quitter joining the segment of the quitter `parent`.
    std::coroutine_handle<> join(
        std::coroutine_handle<> parent,
        detail::quitter_segment::promise_type* seg,
        await_context const* env) noexcept
    {
        auto& p = h_.promise();
        p.seg_ = seg;
        p.set_environment(env);
        p.set_continuation(parent);
        lent_ = true;
        return h_;
    }

    void destroy_frame() noexcept
    {
        if(h_)
        {
            if(lent_ && !h_.done())
            {
                // Only the unwind of an abandoned chain destroys a frame
                // that is still awaited.
                auto* owner = h_.promise().environment()->owner;
                if(!owner || !owner->abandoned())
                    std::terminate(); // LCOV_EXCL_LINE reached only in a death test's child process
            }
            std::exchange(h_, nullptr).destroy();
        }
        if(seg_.h_)
        {
            seg_.h_.promise().active_ = false;
            std::exchange(seg_.h_, nullptr).destroy();
        }
        lent_ = false;
    }
};

} // namespace capy
} // namespace boost

#endif
