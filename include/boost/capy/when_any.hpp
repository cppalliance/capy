//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_WHEN_ANY_HPP
#define BOOST_CAPY_WHEN_ANY_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <coroutine>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/task.hpp>

#include <array>
#include <atomic>
#include <exception>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

/*
   when_any - Race multiple tasks, return first completion
   ========================================================

   OVERVIEW:
   ---------
   when_any launches N tasks concurrently and completes when the FIRST task
   finishes (success or failure). It then requests stop for all siblings and
   waits for them to acknowledge before returning.

   ARCHITECTURE:
   -------------
   The design mirrors when_all but with inverted completion semantics:

     when_all:  complete when remaining_count reaches 0 (all done)
     when_any:  complete when has_winner becomes true (first done)
                BUT still wait for remaining_count to reach 0 for cleanup

   Key components:
     - when_any_state:    Shared state tracking winner and completion
     - when_any_runner:   Wrapper coroutine for each child task
     - when_any_launcher: Awaitable that starts all runners concurrently

   CRITICAL INVARIANTS:
   --------------------
   1. Exactly one task becomes the winner (via atomic compare_exchange)
   2. All tasks must complete before parent resumes (cleanup safety)
   3. Stop is requested immediately when winner is determined
   4. Only the winner's result/exception is stored

   POSITIONAL VARIANT:
   -------------------
   The variadic overload returns a std::variant with one alternative per
   input task, preserving positional correspondence. Use .index() on
   the variant to identify which task won.

   Example: when_any(task<int>, task<string>, task<int>)
     - Raw types after void->monostate: int, string, int
     - Result variant: std::variant<int, string, int>
     - variant.index() tells you which task won (0, 1, or 2)

   VOID HANDLING:
   --------------
   void tasks contribute std::monostate to the variant.
   All-void tasks result in: variant<monostate, monostate, monostate>

   MEMORY MODEL:
   -------------
   Synchronization chain from winner's write to parent's read:

   1. Winner thread writes result_/winner_exception_ (non-atomic)
   2. Winner thread calls signal_completion() → fetch_sub(acq_rel) on remaining_count_
   3. Last task thread (may be winner or non-winner) calls signal_completion()
      → fetch_sub(acq_rel) on remaining_count_, observing count becomes 0
   4. Last task returns caller_ex_.dispatch(continuation_) via symmetric transfer
   5. Parent coroutine resumes and reads result_/winner_exception_

   Synchronization analysis:
   - All fetch_sub operations on remaining_count_ form a release sequence
   - Winner's fetch_sub releases; subsequent fetch_sub operations participate
     in the modification order of remaining_count_
   - Last task's fetch_sub(acq_rel) synchronizes-with prior releases in the
     modification order, establishing happens-before from winner's writes
   - Executor dispatch() is expected to provide queue-based synchronization
     (release-on-post, acquire-on-execute) completing the chain to parent
   - Even inline executors work (same thread = sequenced-before)

   Alternative considered: Adding winner_ready_ atomic (set with release after
   storing winner data, acquired before reading) would make synchronization
   self-contained and not rely on executor implementation details. Current
   approach is correct but requires careful reasoning about release sequences
   and executor behavior.

   EXCEPTION SEMANTICS:
   --------------------
   Unlike when_all (which captures first exception, discards others), when_any
   treats exceptions as valid completions. If the winning task threw, that
   exception is rethrown. Exceptions from non-winners are silently discarded.
*/

namespace boost {
namespace capy {

namespace detail {

/** Convert void to monostate for variant storage.

    std::variant<void, ...> is ill-formed, so void tasks contribute
    std::monostate to the result variant instead. Non-void types
    pass through unchanged.

    @tparam T The type to potentially convert (void becomes monostate).
*/
template<typename T>
using void_to_monostate_t = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

// Result variant: one alternative per task, preserving positional
// correspondence. Use .index() to identify which task won.
// void results become monostate.
template<typename T0, typename... Ts>
using when_any_variant_t = std::variant<void_to_monostate_t<T0>, void_to_monostate_t<Ts>...>;

/** Core shared state for when_any operations.

    Contains all members and methods common to both heterogeneous (variadic)
    and homogeneous (range) when_any implementations. State classes embed
    this via composition to avoid CRTP destructor ordering issues.

    @par Thread Safety
    Atomic operations protect winner selection and completion count.
*/
struct when_any_core
{
    std::atomic<std::size_t> remaining_count_;
    std::size_t winner_index_{0};
    std::exception_ptr winner_exception_;
    std::stop_source stop_source_;

    // Bridges parent's stop token to our stop_source
    struct stop_callback_fn
    {
        std::stop_source* source_;
        void operator()() const noexcept { source_->request_stop(); }
    };
    using stop_callback_t = std::stop_callback<stop_callback_fn>;
    std::optional<stop_callback_t> parent_stop_callback_;

    std::coroutine_handle<> continuation_;
    io_env const* caller_env_ = nullptr;

    // Placed last to avoid padding (1-byte atomic followed by 8-byte aligned members)
    std::atomic<bool> has_winner_{false};

    explicit when_any_core(std::size_t count) noexcept
        : remaining_count_(count)
    {
    }

    /** Atomically claim winner status; exactly one task succeeds. */
    bool try_win(std::size_t index) noexcept
    {
        bool expected = false;
        if(has_winner_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        {
            winner_index_ = index;
            stop_source_.request_stop();
            return true;
        }
        return false;
    }

    /** @pre try_win() returned true. */
    void set_winner_exception(std::exception_ptr ep) noexcept
    {
        winner_exception_ = ep;
    }

    // Runners signal completion directly via final_suspend; no member function needed.
};

/** Shared state for heterogeneous when_any operation.

    Coordinates winner selection, result storage, and completion tracking
    for all child tasks in a when_any operation. Uses composition with
    when_any_core for shared functionality.

    @par Lifetime
    Allocated on the parent coroutine's frame, outlives all runners.

    @tparam T0 First task's result type.
    @tparam Ts Remaining tasks' result types.
*/
template<typename T0, typename... Ts>
struct when_any_state
{
    static constexpr std::size_t task_count = 1 + sizeof...(Ts);
    using variant_type = when_any_variant_t<T0, Ts...>;

    when_any_core core_;
    std::optional<variant_type> result_;
    std::array<std::coroutine_handle<>, task_count> runner_handles_{};

    when_any_state()
        : core_(task_count)
    {
    }

    // Runners self-destruct in final_suspend. No destruction needed here.

    /** @pre core_.try_win() returned true.
        @note Uses in_place_index (not type) for positional variant access.
    */
    template<std::size_t I, typename T>
    void set_winner_result(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        result_.emplace(std::in_place_index<I>, std::move(value));
    }

    /** @pre core_.try_win() returned true. */
    template<std::size_t I>
    void set_winner_void() noexcept
    {
        result_.emplace(std::in_place_index<I>, std::monostate{});
    }
};

/** Wrapper coroutine that runs a single child task for when_any.

    Propagates executor/stop_token to the child, attempts to claim winner
    status on completion, and signals completion for cleanup coordination.

    @tparam StateType The state type (when_any_state or when_any_homogeneous_state).
*/
template<typename StateType>
struct when_any_runner
{
    struct promise_type // : frame_allocating_base  // DISABLED FOR TESTING
    {
        StateType* state_ = nullptr;
        std::size_t index_ = 0;
        io_env env_;

        when_any_runner get_return_object() noexcept
        {
            return when_any_runner(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        // Starts suspended; launcher sets up state/ex/token then resumes
        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() const noexcept { return false; }
                auto await_suspend(std::coroutine_handle<> h) noexcept
                {
                    // Extract everything needed before self-destruction.
                    auto& core = p_->state_->core_;
                    auto* counter = &core.remaining_count_;
                    auto* caller_env = core.caller_env_;
                    auto cont = core.continuation_;

                    h.destroy();

                    // If last runner, dispatch parent for symmetric transfer.
                    auto remaining = counter->fetch_sub(1, std::memory_order_acq_rel);
                    if(remaining == 1)
                        return detail::symmetric_transfer(caller_env->executor.dispatch(cont));
                    return detail::symmetric_transfer(std::noop_coroutine());
                }
                void await_resume() const noexcept {}
            };
            return awaiter{this};
        }

        void return_void() noexcept {}

        // Exceptions are valid completions in when_any (unlike when_all)
        void unhandled_exception()
        {
            if(state_->core_.try_win(index_))
                state_->core_.set_winner_exception(std::current_exception());
        }

        /** Injects executor and stop token into child awaitables. */
        template<class Awaitable>
        struct transform_awaiter
        {
            std::decay_t<Awaitable> a_;
            promise_type* p_;

            bool await_ready() { return a_.await_ready(); }
            auto await_resume() { return a_.await_resume(); }

            template<class Promise>
            auto await_suspend(std::coroutine_handle<Promise> h)
            {
                using R = decltype(a_.await_suspend(h, &p_->env_));
                if constexpr (std::is_same_v<R, std::coroutine_handle<>>)
                    return detail::symmetric_transfer(a_.await_suspend(h, &p_->env_));
                else
                    return a_.await_suspend(h, &p_->env_);
            }
        };

        template<class Awaitable>
        auto await_transform(Awaitable&& a)
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

    std::coroutine_handle<promise_type> h_;

    explicit when_any_runner(std::coroutine_handle<promise_type> h) noexcept
        : h_(h)
    {
    }

    // Enable move for all clang versions - some versions need it
    when_any_runner(when_any_runner&& other) noexcept : h_(std::exchange(other.h_, nullptr)) {}

    // Non-copyable
    when_any_runner(when_any_runner const&) = delete;
    when_any_runner& operator=(when_any_runner const&) = delete;
    when_any_runner& operator=(when_any_runner&&) = delete;

    auto release() noexcept
    {
        return std::exchange(h_, nullptr);
    }
};

/** Indexed overload for heterogeneous when_any (compile-time index).

    Uses compile-time index I for variant construction via in_place_index.
    Called from when_any_launcher::launch_one<I>().
*/
template<std::size_t I, IoAwaitable Awaitable, typename StateType>
when_any_runner<StateType>
make_when_any_runner(Awaitable inner, StateType* state)
{
    using T = awaitable_result_t<Awaitable>;
    if constexpr (std::is_void_v<T>)
    {
        co_await std::move(inner);
        if(state->core_.try_win(I))
            state->template set_winner_void<I>();
    }
    else
    {
        auto result = co_await std::move(inner);
        if(state->core_.try_win(I))
        {
            try
            {
                state->template set_winner_result<I>(std::move(result));
            }
            catch(...)
            {
                state->core_.set_winner_exception(std::current_exception());
            }
        }
    }
}

/** Runtime-index overload for homogeneous when_any (range path).

    Uses requires-expressions to detect state capabilities:
    - set_winner_void(): for heterogeneous void tasks (stores monostate)
    - set_winner_result(): for non-void tasks
    - Neither: for homogeneous void tasks (no result storage)
*/
template<IoAwaitable Awaitable, typename StateType>
when_any_runner<StateType>
make_when_any_runner(Awaitable inner, StateType* state, std::size_t index)
{
    using T = awaitable_result_t<Awaitable>;
    if constexpr (std::is_void_v<T>)
    {
        co_await std::move(inner);
        if(state->core_.try_win(index))
        {
            if constexpr (requires { state->set_winner_void(); })
                state->set_winner_void();
        }
    }
    else
    {
        auto result = co_await std::move(inner);
        if(state->core_.try_win(index))
        {
            try
            {
                state->set_winner_result(std::move(result));
            }
            catch(...)
            {
                state->core_.set_winner_exception(std::current_exception());
            }
        }
    }
}

/** Launches all runners concurrently; see await_suspend for lifetime concerns. */
template<IoAwaitable... Awaitables>
class when_any_launcher
{
    using state_type = when_any_state<awaitable_result_t<Awaitables>...>;

    std::tuple<Awaitables...>* tasks_;
    state_type* state_;

public:
    when_any_launcher(
        std::tuple<Awaitables...>* tasks,
        state_type* state)
        : tasks_(tasks)
        , state_(state)
    {
    }

    bool await_ready() const noexcept
    {
        return sizeof...(Awaitables) == 0;
    }

    /** CRITICAL: If the last task finishes synchronously, parent resumes and
        destroys this object before await_suspend returns. Must not reference
        `this` after the final launch_one call.
    */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation, io_env const* caller_env)
    {
        state_->core_.continuation_ = continuation;
        state_->core_.caller_env_ = caller_env;

        if(caller_env->stop_token.stop_possible())
        {
            state_->core_.parent_stop_callback_.emplace(
                caller_env->stop_token,
                when_any_core::stop_callback_fn{&state_->core_.stop_source_});

            if(caller_env->stop_token.stop_requested())
                state_->core_.stop_source_.request_stop();
        }

        auto token = state_->core_.stop_source_.get_token();
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (..., launch_one<Is>(caller_env->executor, token));
        }(std::index_sequence_for<Awaitables...>{});

        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
    }

private:
    /** @pre Ex::dispatch() and std::coroutine_handle<>::resume() must not throw (handle may leak). */
    template<std::size_t I>
    void launch_one(executor_ref caller_ex, std::stop_token token)
    {
        auto runner = make_when_any_runner<I>(
            std::move(std::get<I>(*tasks_)), state_);

        auto h = runner.release();
        h.promise().state_ = state_;
        h.promise().index_ = I;
        h.promise().env_ = io_env{caller_ex, token, state_->core_.caller_env_->frame_allocator};

        std::coroutine_handle<> ch{h};
        state_->runner_handles_[I] = ch;
        caller_ex.post(ch);
    }
};

} // namespace detail

/** Wait for the first awaitable to complete.

    Races multiple heterogeneous awaitables concurrently and returns when the
    first one completes. The result is a variant with one alternative per
    input task, preserving positional correspondence.

    @par Suspends
    The calling coroutine suspends when co_await is invoked. All awaitables
    are launched concurrently and execute in parallel. The coroutine resumes
    only after all awaitables have completed, even though the winner is
    determined by the first to finish.

    @par Completion Conditions
    @li Winner is determined when the first awaitable completes (success or exception)
    @li Only one task can claim winner status via atomic compare-exchange
    @li Once a winner exists, stop is requested for all remaining siblings
    @li Parent coroutine resumes only after all siblings acknowledge completion
    @li The winner's result is returned; if the winner threw, the exception is rethrown

    @par Cancellation Semantics
    Cancellation is supported via stop_token propagated through the
    IoAwaitable protocol:
    @li Each child awaitable receives a stop_token derived from a shared stop_source
    @li When the parent's stop token is activated, the stop is forwarded to all children
    @li When a winner is determined, stop_source_.request_stop() is called immediately
    @li Siblings must handle cancellation gracefully and complete before parent resumes
    @li Stop requests are cooperative; tasks must check and respond to them

    @par Concurrency/Overlap
    All awaitables are launched concurrently before any can complete.
    The launcher iterates through the arguments, starting each task on the
    caller's executor. Tasks may execute in parallel on multi-threaded
    executors or interleave on single-threaded executors. There is no
    guaranteed ordering of task completion.

    @par Notable Error Conditions
    @li Winner exception: if the winning task threw, that exception is rethrown
    @li Non-winner exceptions: silently discarded (only winner's result matters)
    @li Cancellation: tasks may complete via cancellation without throwing

    @par Example
    @code
    task<void> example() {
        auto result = co_await when_any(
            fetch_int(),      // task<int>
            fetch_string()    // task<std::string>
        );
        // result.index() is 0 or 1
        if (result.index() == 0)
            std::cout << "Got int: " << std::get<0>(result) << "\n";
        else
            std::cout << "Got string: " << std::get<1>(result) << "\n";
    }
    @endcode

    @tparam A0 First awaitable type (must satisfy IoAwaitable).
    @tparam As Remaining awaitable types (must satisfy IoAwaitable).
    @param a0 The first awaitable to race.
    @param as Additional awaitables to race concurrently.
    @return A task yielding a variant with one alternative per awaitable.
        Use .index() to identify the winner. Void awaitables contribute
        std::monostate.

    @throws Rethrows the winner's exception if the winning task threw an exception.

    @par Remarks
    Awaitables are moved into the coroutine frame; original objects become
    empty after the call. The variant preserves one alternative per input
    task. Use .index() to determine which awaitable completed first.
    Void awaitables contribute std::monostate to the variant.

    @see when_all, IoAwaitable
*/
template<IoAwaitable A0, IoAwaitable... As>
[[nodiscard]] auto when_any(A0 a0, As... as)
    -> task<detail::when_any_variant_t<
        detail::awaitable_result_t<A0>,
        detail::awaitable_result_t<As>...>>
{
    detail::when_any_state<
        detail::awaitable_result_t<A0>,
        detail::awaitable_result_t<As>...> state;
    std::tuple<A0, As...> awaitable_tuple(std::move(a0), std::move(as)...);

    co_await detail::when_any_launcher<A0, As...>(&awaitable_tuple, &state);

    if(state.core_.winner_exception_)
        std::rethrow_exception(state.core_.winner_exception_);

    co_return std::move(*state.result_);
}

/** Concept for ranges of full I/O awaitables.

    A range satisfies `IoAwaitableRange` if it is a sized input range
    whose value type satisfies @ref IoAwaitable. This enables when_any
    to accept any container or view of awaitables, not just std::vector.

    @tparam R The range type.

    @par Requirements
    @li `R` must satisfy `std::ranges::input_range`
    @li `R` must satisfy `std::ranges::sized_range`
    @li `std::ranges::range_value_t<R>` must satisfy @ref IoAwaitable

    @par Syntactic Requirements
    Given `r` of type `R`:
    @li `std::ranges::begin(r)` is valid
    @li `std::ranges::end(r)` is valid
    @li `std::ranges::size(r)` returns `std::ranges::range_size_t<R>`
    @li `*std::ranges::begin(r)` satisfies @ref IoAwaitable

    @par Example
    @code
    template<IoAwaitableRange R>
    task<void> race_all(R&& awaitables) {
        auto winner = co_await when_any(std::forward<R>(awaitables));
        // Process winner...
    }
    @endcode

    @see when_any, IoAwaitable
*/
template<typename R>
concept IoAwaitableRange =
    std::ranges::input_range<R> &&
    std::ranges::sized_range<R> &&
    IoAwaitable<std::ranges::range_value_t<R>>;

namespace detail {

/** Shared state for homogeneous when_any (range overload).

    Uses composition with when_any_core for shared functionality.
    Simpler than heterogeneous: optional<T> instead of variant, vector
    instead of array for runner handles.
*/
template<typename T>
struct when_any_homogeneous_state
{
    when_any_core core_;
    std::optional<T> result_;
    std::vector<std::coroutine_handle<>> runner_handles_;

    explicit when_any_homogeneous_state(std::size_t count)
        : core_(count)
        , runner_handles_(count)
    {
    }

    // Runners self-destruct in final_suspend. No destruction needed here.

    /** @pre core_.try_win() returned true. */
    void set_winner_result(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        result_.emplace(std::move(value));
    }
};

/** Specialization for void tasks (no result storage needed). */
template<>
struct when_any_homogeneous_state<void>
{
    when_any_core core_;
    std::vector<std::coroutine_handle<>> runner_handles_;

    explicit when_any_homogeneous_state(std::size_t count)
        : core_(count)
        , runner_handles_(count)
    {
    }

    // Runners self-destruct in final_suspend. No destruction needed here.

    // No set_winner_result - void tasks have no result to store
};

/** Launches all runners concurrently; see await_suspend for lifetime concerns. */
template<IoAwaitableRange Range>
class when_any_homogeneous_launcher
{
    using Awaitable = std::ranges::range_value_t<Range>;
    using T = awaitable_result_t<Awaitable>;

    Range* range_;
    when_any_homogeneous_state<T>* state_;

public:
    when_any_homogeneous_launcher(
        Range* range,
        when_any_homogeneous_state<T>* state)
        : range_(range)
        , state_(state)
    {
    }

    bool await_ready() const noexcept
    {
        return std::ranges::empty(*range_);
    }

    /** CRITICAL: If the last task finishes synchronously, parent resumes and
        destroys this object before await_suspend returns. Must not reference
        `this` after dispatching begins.

        Two-phase approach:
        1. Create all runners (safe - no dispatch yet)
        2. Dispatch all runners (any may complete synchronously)
    */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation, io_env const* caller_env)
    {
        state_->core_.continuation_ = continuation;
        state_->core_.caller_env_ = caller_env;

        if(caller_env->stop_token.stop_possible())
        {
            state_->core_.parent_stop_callback_.emplace(
                caller_env->stop_token,
                when_any_core::stop_callback_fn{&state_->core_.stop_source_});

            if(caller_env->stop_token.stop_requested())
                state_->core_.stop_source_.request_stop();
        }

        auto token = state_->core_.stop_source_.get_token();

        // Phase 1: Create all runners without dispatching.
        // This iterates over *range_ safely because no runners execute yet.
        std::size_t index = 0;
        for(auto&& a : *range_)
        {
            auto runner = make_when_any_runner(
                std::move(a), state_, index);

            auto h = runner.release();
            h.promise().state_ = state_;
            h.promise().index_ = index;
            h.promise().env_ = io_env{caller_env->executor, token, caller_env->frame_allocator};

            state_->runner_handles_[index] = std::coroutine_handle<>{h};
            ++index;
        }

        // Phase 2: Post all runners. Any may complete synchronously.
        // After last post, state_ and this may be destroyed.
        // Use raw pointer/count captured before posting.
        std::coroutine_handle<>* handles = state_->runner_handles_.data();
        std::size_t count = state_->runner_handles_.size();
        for(std::size_t i = 0; i < count; ++i)
            caller_env->executor.post(handles[i]);

        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
    }
};

} // namespace detail

/** Wait for the first awaitable to complete (range overload).

    Races a range of awaitables with the same result type. Accepts any
    sized input range of IoAwaitable types, enabling use with arrays,
    spans, or custom containers.

    @par Suspends
    The calling coroutine suspends when co_await is invoked. All awaitables
    in the range are launched concurrently and execute in parallel. The
    coroutine resumes only after all awaitables have completed, even though
    the winner is determined by the first to finish.

    @par Completion Conditions
    @li Winner is determined when the first awaitable completes (success or exception)
    @li Only one task can claim winner status via atomic compare-exchange
    @li Once a winner exists, stop is requested for all remaining siblings
    @li Parent coroutine resumes only after all siblings acknowledge completion
    @li The winner's index and result are returned; if the winner threw, the exception is rethrown

    @par Cancellation Semantics
    Cancellation is supported via stop_token propagated through the
    IoAwaitable protocol:
    @li Each child awaitable receives a stop_token derived from a shared stop_source
    @li When the parent's stop token is activated, the stop is forwarded to all children
    @li When a winner is determined, stop_source_.request_stop() is called immediately
    @li Siblings must handle cancellation gracefully and complete before parent resumes
    @li Stop requests are cooperative; tasks must check and respond to them

    @par Concurrency/Overlap
    All awaitables are launched concurrently before any can complete.
    The launcher iterates through the range, starting each task on the
    caller's executor. Tasks may execute in parallel on multi-threaded
    executors or interleave on single-threaded executors. There is no
    guaranteed ordering of task completion.

    @par Notable Error Conditions
    @li Empty range: throws std::invalid_argument immediately (not via co_return)
    @li Winner exception: if the winning task threw, that exception is rethrown
    @li Non-winner exceptions: silently discarded (only winner's result matters)
    @li Cancellation: tasks may complete via cancellation without throwing

    @par Example
    @code
    task<void> example() {
        std::array<task<Response>, 3> requests = {
            fetch_from_server(0),
            fetch_from_server(1),
            fetch_from_server(2)
        };

        auto [index, response] = co_await when_any(std::move(requests));
    }
    @endcode

    @par Example with Vector
    @code
    task<Response> fetch_fastest(std::vector<Server> const& servers) {
        std::vector<task<Response>> requests;
        for (auto const& server : servers)
            requests.push_back(fetch_from(server));

        auto [index, response] = co_await when_any(std::move(requests));
        co_return response;
    }
    @endcode

    @tparam R Range type satisfying IoAwaitableRange.
    @param awaitables Range of awaitables to race concurrently (must not be empty).
    @return A task yielding a pair of (winner_index, result).

    @throws std::invalid_argument if range is empty (thrown before coroutine suspends).
    @throws Rethrows the winner's exception if the winning task threw an exception.

    @par Remarks
    Elements are moved from the range; for lvalue ranges, the original
    container will have moved-from elements after this call. The range
    is moved onto the coroutine frame to ensure lifetime safety. Unlike
    the variadic overload, no variant wrapper is needed since all tasks
    share the same return type.

    @see when_any, IoAwaitableRange
*/
template<IoAwaitableRange R>
    requires (!std::is_void_v<detail::awaitable_result_t<std::ranges::range_value_t<R>>>)
[[nodiscard]] auto when_any(R&& awaitables)
    -> task<std::pair<std::size_t, detail::awaitable_result_t<std::ranges::range_value_t<R>>>>
{
    using Awaitable = std::ranges::range_value_t<R>;
    using T = detail::awaitable_result_t<Awaitable>;
    using result_type = std::pair<std::size_t, T>;
    using OwnedRange = std::remove_cvref_t<R>;

    auto count = std::ranges::size(awaitables);
    if(count == 0)
        throw std::invalid_argument("when_any requires at least one awaitable");

    // Move/copy range onto coroutine frame to ensure lifetime
    OwnedRange owned_awaitables = std::forward<R>(awaitables);

    detail::when_any_homogeneous_state<T> state(count);

    co_await detail::when_any_homogeneous_launcher<OwnedRange>(&owned_awaitables, &state);

    if(state.core_.winner_exception_)
        std::rethrow_exception(state.core_.winner_exception_);

    co_return result_type{state.core_.winner_index_, std::move(*state.result_)};
}

/** Wait for the first awaitable to complete (void range overload).

    Races a range of void-returning awaitables. Since void awaitables have
    no result value, only the winner's index is returned.

    @par Suspends
    The calling coroutine suspends when co_await is invoked. All awaitables
    in the range are launched concurrently and execute in parallel. The
    coroutine resumes only after all awaitables have completed, even though
    the winner is determined by the first to finish.

    @par Completion Conditions
    @li Winner is determined when the first awaitable completes (success or exception)
    @li Only one task can claim winner status via atomic compare-exchange
    @li Once a winner exists, stop is requested for all remaining siblings
    @li Parent coroutine resumes only after all siblings acknowledge completion
    @li The winner's index is returned; if the winner threw, the exception is rethrown

    @par Cancellation Semantics
    Cancellation is supported via stop_token propagated through the
    IoAwaitable protocol:
    @li Each child awaitable receives a stop_token derived from a shared stop_source
    @li When the parent's stop token is activated, the stop is forwarded to all children
    @li When a winner is determined, stop_source_.request_stop() is called immediately
    @li Siblings must handle cancellation gracefully and complete before parent resumes
    @li Stop requests are cooperative; tasks must check and respond to them

    @par Concurrency/Overlap
    All awaitables are launched concurrently before any can complete.
    The launcher iterates through the range, starting each task on the
    caller's executor. Tasks may execute in parallel on multi-threaded
    executors or interleave on single-threaded executors. There is no
    guaranteed ordering of task completion.

    @par Notable Error Conditions
    @li Empty range: throws std::invalid_argument immediately (not via co_return)
    @li Winner exception: if the winning task threw, that exception is rethrown
    @li Non-winner exceptions: silently discarded (only winner's result matters)
    @li Cancellation: tasks may complete via cancellation without throwing

    @par Example
    @code
    task<void> example() {
        std::vector<task<void>> tasks;
        for (int i = 0; i < 5; ++i)
            tasks.push_back(background_work(i));

        std::size_t winner = co_await when_any(std::move(tasks));
        // winner is the index of the first task to complete
    }
    @endcode

    @par Example with Timeout
    @code
    task<void> with_timeout() {
        std::vector<task<void>> tasks;
        tasks.push_back(long_running_operation());
        tasks.push_back(delay(std::chrono::seconds(5)));

        std::size_t winner = co_await when_any(std::move(tasks));
        if (winner == 1) {
            // Timeout occurred
        }
    }
    @endcode

    @tparam R Range type satisfying IoAwaitableRange with void result.
    @param awaitables Range of void awaitables to race concurrently (must not be empty).
    @return A task yielding the winner's index (zero-based).

    @throws std::invalid_argument if range is empty (thrown before coroutine suspends).
    @throws Rethrows the winner's exception if the winning task threw an exception.

    @par Remarks
    Elements are moved from the range; for lvalue ranges, the original
    container will have moved-from elements after this call. The range
    is moved onto the coroutine frame to ensure lifetime safety. Unlike
    the non-void overload, no result storage is needed since void tasks
    produce no value.

    @see when_any, IoAwaitableRange
*/
template<IoAwaitableRange R>
    requires std::is_void_v<detail::awaitable_result_t<std::ranges::range_value_t<R>>>
[[nodiscard]] auto when_any(R&& awaitables) -> task<std::size_t>
{
    using OwnedRange = std::remove_cvref_t<R>;

    auto count = std::ranges::size(awaitables);
    if(count == 0)
        throw std::invalid_argument("when_any requires at least one awaitable");

    // Move/copy range onto coroutine frame to ensure lifetime
    OwnedRange owned_awaitables = std::forward<R>(awaitables);

    detail::when_any_homogeneous_state<void> state(count);

    co_await detail::when_any_homogeneous_launcher<OwnedRange>(&owned_awaitables, &state);

    if(state.core_.winner_exception_)
        std::rethrow_exception(state.core_.winner_exception_);

    co_return state.core_.winner_index_;
}

} // namespace capy
} // namespace boost

#endif
