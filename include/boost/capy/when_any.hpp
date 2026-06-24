//
// Copyright (c) 2026 Michael Vandeberg
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_WHEN_ANY_HPP
#define BOOST_CAPY_WHEN_ANY_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/io_result_combinators.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <coroutine>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/frame_alloc_mixin.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/task.hpp>

#include <array>
#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
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
   when_any - Race multiple io_result tasks, select first success
   =============================================================

   OVERVIEW:
   ---------
   when_any launches N io_result-returning tasks concurrently. A task
   wins by returning !ec; errors and exceptions do not win. Once a
   winner is found, stop is requested for siblings and the winner's
   payload is returned. If no winner exists (all fail), one of the
   failures is surfaced (an error_code at variant index 0, or a child's
   exception rethrown); which one is unspecified.

   ARCHITECTURE:
   -------------
   The design mirrors when_all but with inverted completion semantics:

     when_all:  complete when remaining_count reaches 0 (all done)
     when_any:  complete when has_winner becomes true (first done)
                BUT still wait for remaining_count to reach 0 for cleanup

   Key components:
     - when_any_core:    Shared state tracking winner and completion
     - when_any_io_runner: Wrapper coroutine for each child task
     - when_any_io_launcher/when_any_io_homogeneous_launcher:
                          Awaitables that start all runners concurrently

   CRITICAL INVARIANTS:
   --------------------
   1. Only a task returning !ec can become the winner (via atomic CAS)
   2. All tasks must complete before parent resumes (cleanup safety)
   3. Stop is requested immediately when winner is determined
   4. Exceptions and errors do not claim winner status

   POSITIONAL VARIANT:
   -------------------
   The variadic overload returns std::variant<error_code, R1, R2, ..., Rn>.
   Index 0 is error_code (failure/no-winner). Index 1..N identifies the
   winning child and carries its payload.

   RANGE OVERLOAD:
   ---------------
   The range overload returns variant<error_code, pair<size_t, T>> for
   non-void children or variant<error_code, size_t> for void children.

   MEMORY MODEL:
   -------------
   Synchronization chain from winner's write to parent's read:

   1. Winner thread writes result_ (non-atomic)
   2. Winner thread calls signal_completion() -> fetch_sub(acq_rel) on remaining_count_
   3. Last task thread (may be winner or non-winner) calls signal_completion()
      -> fetch_sub(acq_rel) on remaining_count_, observing count becomes 0
   4. Last task returns caller_ex_.dispatch(continuation_) via symmetric transfer
   5. Parent coroutine resumes and reads result_

   Synchronization analysis:
   - All fetch_sub operations on remaining_count_ form a release sequence
   - Winner's fetch_sub releases; subsequent fetch_sub operations participate
     in the modification order of remaining_count_
   - Last task's fetch_sub(acq_rel) synchronizes-with prior releases in the
     modification order, establishing happens-before from winner's writes
   - Executor dispatch() is expected to provide queue-based synchronization
     (release-on-post, acquire-on-execute) completing the chain to parent
   - Even inline executors work (same thread = sequenced-before)

   EXCEPTION SEMANTICS:
   --------------------
   Exceptions do NOT claim winner status. If a child throws, the exception
   is recorded but the combinator keeps waiting for a success. Only when
   all children complete without a winner is a failure surfaced. There is
   no priority between errors and exceptions, and no guarantee about which
   child's failure is reported: the result either returns an error_code at
   variant index 0 or rethrows a child's exception.
*/

namespace boost {
namespace capy {

namespace detail {

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

    continuation continuation_;
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

} // namespace detail

namespace detail {

// State for io_result-aware when_any: only !ec wins.
template<typename... Ts>
struct when_any_io_state
{
    static constexpr std::size_t task_count = sizeof...(Ts);
    using variant_type = std::variant<std::error_code, Ts...>;

    when_any_core core_;
    std::optional<variant_type> result_;
    std::array<continuation, task_count> runner_handles_{};

    // A failure (error or exception) for the all-fail case. record_error
    // and record_exception overwrite each other, so which one survives is
    // unspecified (no priority between errors and exceptions).
    std::mutex failure_mu_;
    std::error_code last_error_;
    std::exception_ptr last_exception_;

    when_any_io_state()
        : core_(task_count)
    {
    }

    void record_error(std::error_code ec)
    {
        std::lock_guard lk(failure_mu_);
        last_error_ = ec;
        last_exception_ = nullptr;
    }

    void record_exception(std::exception_ptr ep)
    {
        std::lock_guard lk(failure_mu_);
        last_exception_ = ep;
        last_error_ = {};
    }
};

// Wrapper coroutine for io_result-aware when_any children.
// unhandled_exception records the exception but does NOT claim winner status.
template<typename StateType>
struct BOOST_CAPY_CORO_DESTROY_WHEN_COMPLETE when_any_io_runner
{
    struct promise_type
        : frame_alloc_mixin
    {
        StateType* state_ = nullptr;
        std::size_t index_ = 0;
        io_env env_;

        when_any_io_runner get_return_object() noexcept
        {
            return when_any_io_runner(
                std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() const noexcept { return false; }
                auto await_suspend(std::coroutine_handle<> h) noexcept
                {
                    auto& core = p_->state_->core_;
                    auto* counter = &core.remaining_count_;
                    auto* caller_env = core.caller_env_;
                    auto& cont = core.continuation_;

                    h.destroy();

                    auto remaining = counter->fetch_sub(1, std::memory_order_acq_rel);
                    if(remaining == 1)
                        return detail::symmetric_transfer(caller_env->executor.dispatch(cont));
                    return detail::symmetric_transfer(std::noop_coroutine());
                }
                void await_resume() const noexcept {} // LCOV_EXCL_LINE final_suspend awaiter, never resumed
            };
            return awaiter{this};
        }

        void return_void() noexcept {}

        // Exceptions do NOT win in io_result when_any
        void unhandled_exception() noexcept
        {
            state_->record_exception(std::current_exception());
        }

        template<class Awaitable>
        struct transform_awaiter
        {
            std::decay_t<Awaitable> a_;
            promise_type* p_;

            bool await_ready() { return a_.await_ready(); }
            decltype(auto) await_resume() { return a_.await_resume(); }

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

    explicit when_any_io_runner(std::coroutine_handle<promise_type> h) noexcept
        : h_(h)
    {
    }

    when_any_io_runner(when_any_io_runner&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    when_any_io_runner(when_any_io_runner const&) = delete;
    when_any_io_runner& operator=(when_any_io_runner const&) = delete;
    when_any_io_runner& operator=(when_any_io_runner&&) = delete;

    auto release() noexcept
    {
        return std::exchange(h_, nullptr);
    }
};

// Runner coroutine: only tries to win when the child returns !ec.
template<std::size_t I, IoAwaitable Awaitable, typename StateType>
when_any_io_runner<StateType>
make_when_any_io_runner(Awaitable inner, StateType* state)
{
    auto result = co_await std::move(inner);

    if(!result.ec)
    {
        // Success: try to claim winner
        if(state->core_.try_win(I))
        {
            try
            {
                state->result_.emplace(
                    std::in_place_index<I + 1>,
                    detail::extract_io_payload(std::move(result)));
            }
            catch(...)
            {
                state->core_.set_winner_exception(std::current_exception());
            }
        }
    }
    else
    {
        // Error: record but don't win
        state->record_error(result.ec);
    }
}

// Launcher for io_result-aware when_any.
template<IoAwaitable... Awaitables>
class when_any_io_launcher
{
    using state_type = when_any_io_state<
        io_result_payload_t<awaitable_result_t<Awaitables>>...>;

    std::tuple<Awaitables...>* tasks_;
    state_type* state_;

public:
    when_any_io_launcher(
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

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> continuation, io_env const* caller_env)
    {
        state_->core_.continuation_.h = continuation;
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
        launch_all(std::index_sequence_for<Awaitables...>{},
            caller_env->executor, token);

        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}

private:
    template<std::size_t... Is>
    void launch_all(std::index_sequence<Is...>,
        executor_ref ex, std::stop_token token)
    {
        (..., launch_one<Is>(ex, token));
    }

    template<std::size_t I>
    void launch_one(executor_ref caller_ex, std::stop_token token)
    {
        auto runner = make_when_any_io_runner<I>(
            std::move(std::get<I>(*tasks_)), state_);

        auto h = runner.release();
        h.promise().state_ = state_;
        h.promise().index_ = I;
        h.promise().env_ = io_env{caller_ex, token,
            state_->core_.caller_env_->frame_allocator};

        state_->runner_handles_[I].h = std::coroutine_handle<>{h};
        caller_ex.post(state_->runner_handles_[I]);
    }
};

/** Shared state for homogeneous io_result-aware when_any (range overload).

    @tparam T The payload type extracted from io_result.
*/
template<typename T>
struct when_any_io_homogeneous_state
{
    when_any_core core_;
    std::optional<T> result_;
    std::unique_ptr<continuation[]> runner_handles_;

    std::mutex failure_mu_;
    std::error_code last_error_;
    std::exception_ptr last_exception_;

    explicit when_any_io_homogeneous_state(std::size_t count)
        : core_(count)
        , runner_handles_(std::make_unique<continuation[]>(count))
    {
    }

    void record_error(std::error_code ec)
    {
        std::lock_guard lk(failure_mu_);
        last_error_ = ec;
        last_exception_ = nullptr;
    }

    void record_exception(std::exception_ptr ep)
    {
        std::lock_guard lk(failure_mu_);
        last_exception_ = ep;
        last_error_ = {};
    }
};

/** Specialization for void io_result children (no payload storage). */
template<>
struct when_any_io_homogeneous_state<std::tuple<>>
{
    when_any_core core_;
    std::unique_ptr<continuation[]> runner_handles_;

    std::mutex failure_mu_;
    std::error_code last_error_;
    std::exception_ptr last_exception_;

    explicit when_any_io_homogeneous_state(std::size_t count)
        : core_(count)
        , runner_handles_(std::make_unique<continuation[]>(count))
    {
    }

    void record_error(std::error_code ec)
    {
        std::lock_guard lk(failure_mu_);
        last_error_ = ec;
        last_exception_ = nullptr;
    }

    void record_exception(std::exception_ptr ep)
    {
        std::lock_guard lk(failure_mu_);
        last_exception_ = ep;
        last_error_ = {};
    }
};

/** Create an io_result-aware runner for homogeneous when_any (range path).

    Only tries to win when the child returns !ec.
*/
template<IoAwaitable Awaitable, typename StateType>
when_any_io_runner<StateType>
make_when_any_io_homogeneous_runner(
    Awaitable inner, StateType* state, std::size_t index)
{
    auto result = co_await std::move(inner);

    if(!result.ec)
    {
        if(state->core_.try_win(index))
        {
            using PayloadT = io_result_payload_t<
                awaitable_result_t<Awaitable>>;
            if constexpr (!std::is_same_v<PayloadT, std::tuple<>>)
            {
                try
                {
                    state->result_.emplace(
                        extract_io_payload(std::move(result)));
                }
                catch(...)
                {
                    state->core_.set_winner_exception(
                        std::current_exception());
                }
            }
        }
    }
    else
    {
        state->record_error(result.ec);
    }
}

/** Launches all io_result-aware homogeneous runners concurrently. */
template<IoAwaitableRange Range>
class when_any_io_homogeneous_launcher
{
    using Awaitable = std::ranges::range_value_t<Range>;
    using PayloadT = io_result_payload_t<awaitable_result_t<Awaitable>>;

    Range* range_;
    when_any_io_homogeneous_state<PayloadT>* state_;

public:
    when_any_io_homogeneous_launcher(
        Range* range,
        when_any_io_homogeneous_state<PayloadT>* state)
        : range_(range)
        , state_(state)
    {
    }

    bool await_ready() const noexcept
    {
        return std::ranges::empty(*range_);
    }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> continuation, io_env const* caller_env)
    {
        state_->core_.continuation_.h = continuation;
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
        std::size_t index = 0;
        for(auto&& a : *range_)
        {
            auto runner = make_when_any_io_homogeneous_runner(
                std::move(a), state_, index);

            auto h = runner.release();
            h.promise().state_ = state_;
            h.promise().index_ = index;
            h.promise().env_ = io_env{caller_env->executor, token,
                caller_env->frame_allocator};

            state_->runner_handles_[index].h = std::coroutine_handle<>{h};
            ++index;
        }

        // Phase 2: Post all runners. Any may complete synchronously.
        auto* handles = state_->runner_handles_.get();
        std::size_t count = state_->core_.remaining_count_.load(std::memory_order_relaxed);
        for(std::size_t i = 0; i < count; ++i)
            caller_env->executor.post(handles[i]);

        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

} // namespace detail

/** Race a range of io_result-returning awaitables (non-void payloads).

    Only a child returning !ec can win. Errors and exceptions do not
    claim winner status. If all children fail, an unspecified one of
    the failures is reported — either an error_code at variant index 0,
    or a child's exception rethrown.

    @param awaitables Range of io_result-returning awaitables (must
        not be empty).

    @return A task yielding variant<error_code, pair<size_t, PayloadT>>
        where index 0 is failure and index 1 carries the winner's
        index and payload.

    @throws std::invalid_argument if range is empty.
    @throws Rethrows the winner's exception if extracting or
        move-constructing the winning payload throws (a winner was
        found but its result could not be produced).
    @throws Rethrows a child's exception when all children fail and the
        reported failure is an exception (which child is unspecified).

    @par Example
    @code
    task<void> example()
    {
        std::vector<io_task<size_t>> reads;
        for (auto& buf : buffers)
            reads.push_back(stream.read_some(buf));

        auto result = co_await when_any(std::move(reads));
        if (result.index() == 1)
        {
            auto [idx, n] = std::get<1>(result);
        }
    }
    @endcode

    @see IoAwaitableRange, when_any
*/
template<IoAwaitableRange R>
    requires detail::is_io_result_v<
        awaitable_result_t<std::ranges::range_value_t<R>>>
    && (!std::is_same_v<
            detail::io_result_payload_t<
                awaitable_result_t<std::ranges::range_value_t<R>>>,
            std::tuple<>>)
[[nodiscard]] auto when_any(R&& awaitables)
    -> task<std::variant<std::error_code,
        std::pair<std::size_t,
            detail::io_result_payload_t<
                awaitable_result_t<std::ranges::range_value_t<R>>>>>>
{
    using Awaitable = std::ranges::range_value_t<R>;
    using PayloadT = detail::io_result_payload_t<
        awaitable_result_t<Awaitable>>;
    using result_type = std::variant<std::error_code,
        std::pair<std::size_t, PayloadT>>;
    using OwnedRange = std::remove_cvref_t<R>;

    auto count = std::ranges::size(awaitables);
    if(count == 0)
        throw std::invalid_argument("when_any requires at least one awaitable");

    OwnedRange owned_awaitables = std::forward<R>(awaitables);

    detail::when_any_io_homogeneous_state<PayloadT> state(count);

    co_await detail::when_any_io_homogeneous_launcher<OwnedRange>(
        &owned_awaitables, &state);

    // Winner found
    if(state.core_.has_winner_.load(std::memory_order_acquire))
    {
        if(state.core_.winner_exception_)
            std::rethrow_exception(state.core_.winner_exception_);
        co_return result_type{std::in_place_index<1>,
            std::pair{state.core_.winner_index_, std::move(*state.result_)}};
    }

    // No winner — report the recorded failure
    if(state.last_exception_)
        std::rethrow_exception(state.last_exception_);
    co_return result_type{std::in_place_index<0>, state.last_error_};
}

/** Race a range of void io_result-returning awaitables.

    Only a child returning !ec can win. Returns the winner's index
    at variant index 1, or error_code at index 0 on all-fail.

    @param awaitables Range of io_result<>-returning awaitables (must
        not be empty).

    @return A task yielding variant<error_code, size_t> where index 0
        is failure and index 1 carries the winner's index.

    @throws std::invalid_argument if range is empty.
    @throws Rethrows a child's exception when all children fail and the
        reported failure is an exception (which child is unspecified).

    @par Example
    @code
    task<void> example()
    {
        std::vector<io_task<>> jobs;
        jobs.push_back(background_work_a());
        jobs.push_back(background_work_b());

        auto result = co_await when_any(std::move(jobs));
        if (result.index() == 1)
        {
            auto winner = std::get<1>(result);
        }
    }
    @endcode

    @see IoAwaitableRange, when_any
*/
template<IoAwaitableRange R>
    requires detail::is_io_result_v<
        awaitable_result_t<std::ranges::range_value_t<R>>>
    && std::is_same_v<
            detail::io_result_payload_t<
                awaitable_result_t<std::ranges::range_value_t<R>>>,
            std::tuple<>>
[[nodiscard]] auto when_any(R&& awaitables)
    -> task<std::variant<std::error_code, std::size_t>>
{
    using OwnedRange = std::remove_cvref_t<R>;
    using result_type = std::variant<std::error_code, std::size_t>;

    auto count = std::ranges::size(awaitables);
    if(count == 0)
        throw std::invalid_argument("when_any requires at least one awaitable");

    OwnedRange owned_awaitables = std::forward<R>(awaitables);

    detail::when_any_io_homogeneous_state<std::tuple<>> state(count);

    co_await detail::when_any_io_homogeneous_launcher<OwnedRange>(
        &owned_awaitables, &state);

    // Winner found
    if(state.core_.has_winner_.load(std::memory_order_acquire))
    {
        if(state.core_.winner_exception_)
            std::rethrow_exception(state.core_.winner_exception_);
        co_return result_type{std::in_place_index<1>,
            state.core_.winner_index_};
    }

    // No winner — report the recorded failure
    if(state.last_exception_)
        std::rethrow_exception(state.last_exception_);
    co_return result_type{std::in_place_index<0>, state.last_error_};
}

/** Race io_result-returning awaitables, selecting the first success.

    Overload selected when all children return io_result<Ts...>.
    Only a child returning !ec can win. Errors and exceptions do
    not claim winner status.

    @param as The awaitables to race. Each must satisfy @ref
        IoAwaitable and is consumed (moved-from) when `when_any`
        is awaited.

    @return A task yielding variant<error_code, R1, ..., Rn> where
        index 0 is the failure/no-winner case and index i+1
        identifies the winning child. On all-fail, index 0 holds
        an error_code from one of the failed children (unspecified
        which; no priority between errors and exceptions).

    @throws Rethrows the winner's exception if extracting or
        constructing the winning payload throws (a winner was found
        but its result could not be produced).
    @throws Rethrows a child's exception when all children fail and the
        reported failure is an exception (which child is unspecified).

    @note A failing child does not cancel its siblings; `when_any`
        waits for a success or for every child to finish. To make a
        benign error (e.g. @c cond::canceled) count as a win, wrap
        the child to translate the error into success. See the
        Concurrent Composition tutorial.
*/
template<IoAwaitable... As>
    requires (sizeof...(As) > 0)
          && detail::all_io_result_awaitables<As...>
[[nodiscard]] auto when_any(As... as)
    -> task<std::variant<
        std::error_code,
        detail::io_result_payload_t<awaitable_result_t<As>>...>>
{
    using result_type = std::variant<
        std::error_code,
        detail::io_result_payload_t<awaitable_result_t<As>>...>;

    detail::when_any_io_state<
        detail::io_result_payload_t<awaitable_result_t<As>>...> state;
    std::tuple<As...> awaitable_tuple(std::move(as)...);

    co_await detail::when_any_io_launcher<As...>(
        &awaitable_tuple, &state);

    // Winner found: return their result
    if(state.result_.has_value())
        co_return std::move(*state.result_);

    // Winner claimed but payload construction failed
    if(state.core_.winner_exception_)
        std::rethrow_exception(state.core_.winner_exception_);

    // No winner — report the recorded failure
    if(state.last_exception_)
        std::rethrow_exception(state.last_exception_);
    co_return result_type{std::in_place_index<0>, state.last_error_};
}

} // namespace capy
} // namespace boost

#endif
