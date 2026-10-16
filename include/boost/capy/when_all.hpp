//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_WHEN_ALL_HPP
#define BOOST_CAPY_WHEN_ALL_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/results_combinators.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/context_awaitable.hpp>
#include <coroutine>
#include <boost/capy/detail/abandonable.hpp>
#include <boost/capy/detail/owned_range.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/frame_alloc_mixin.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/task.hpp>

#include <array>
#include <atomic>
#include <exception>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace boost {
namespace capy {

namespace detail {

/** Holds the result of a single task within when_all.
*/
template<typename T>
struct result_holder
{
    std::optional<T> value_;

    void set(T v)
    {
        value_ = std::move(v);
    }

    T get() &&
    {
        return std::move(*value_);
    }
};

/** Core shared state for when_all operations.

    Contains all members and methods common to both heterogeneous (variadic)
    and homogeneous (range) when_all implementations. State classes embed
    this via composition to avoid CRTP destructor ordering issues.

    @par Thread Safety
    Atomic operations protect exception capture and completion count.
*/
struct when_all_core
{
    std::atomic<std::size_t> remaining_count_;

    // Exception storage - first error wins, others discarded
    std::atomic<bool> has_exception_{false};
    std::exception_ptr first_exception_;

    std::stop_source stop_source_;

    // Bridges parent's stop token to our stop_source
    struct stop_callback_fn
    {
        std::stop_source* source_;
        void operator()() const
        {
            // Stopping can lose the last branch and destroy the join,
            // with its source, before the request returns; the copy
            // keeps the stop state alive until then.
            std::stop_source keep = *source_;
            keep.request_stop();
        }
    };
    using stop_callback_t = std::stop_callback<stop_callback_fn>;
    std::optional<stop_callback_t> parent_stop_callback_;

    continuation continuation_;
    await_context const* caller_env_ = nullptr;

    // Set when a branch is abandoned before finishing: the parent is
    // then abandoned rather than resumed.
    std::atomic<bool> lost_{false};

    /// Drop a share held by a runner that never finished.
    void lose_branch() noexcept
    {
        lost_.store(true, std::memory_order_relaxed);
        stop_source_.request_stop();
        if(remaining_count_.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            auto parent = continuation_;
            capy::abandon(parent);
        }
    }

    explicit when_all_core(std::size_t count) noexcept
        : remaining_count_(count)
    {
    }

    /** Capture an exception (first one wins). */
    void capture_exception(std::exception_ptr ep)
    {
        bool expected = false;
        if(has_exception_.compare_exchange_strong(
            expected, true, std::memory_order_relaxed))
            first_exception_ = ep;
    }
};

/** Shared state for heterogeneous when_all (variadic overload).

    @tparam Ts The result types of the tasks.
*/
template<typename... Ts>
struct when_all_state
{
    static constexpr std::size_t task_count = sizeof...(Ts);

    when_all_core core_;
    std::tuple<result_holder<Ts>...> results_;
    std::array<continuation, task_count> runner_handles_{};

    std::atomic<bool> has_error_{false};
    std::error_code first_error_;

    when_all_state()
        : core_(task_count)
    {
    }

    /** Record the first error (subsequent errors are discarded). */
    void record_error(std::error_code ec)
    {
        bool expected = false;
        if(has_error_.compare_exchange_strong(
            expected, true, std::memory_order_relaxed))
            first_error_ = ec;
    }
};

/** Shared state for homogeneous when_all (range overload).

    Stores extracted results payloads in a vector indexed by task
    position. Tracks the first error_code for error propagation.

    @tparam T The payload type extracted from `results`.
*/
template<typename T>
struct when_all_homogeneous_state
{
    when_all_core core_;
    std::vector<std::optional<T>> results_;
    std::unique_ptr<continuation[]> runner_handles_;

    std::atomic<bool> has_error_{false};
    std::error_code first_error_;

    explicit when_all_homogeneous_state(std::size_t count)
        : core_(count)
        , results_(count)
        , runner_handles_(std::make_unique<continuation[]>(count))
    {
    }

    void set_result(std::size_t index, T value)
    {
        results_[index].emplace(std::move(value));
    }

    /** Record the first error (subsequent errors are discarded). */
    void record_error(std::error_code ec)
    {
        bool expected = false;
        if(has_error_.compare_exchange_strong(
            expected, true, std::memory_order_relaxed))
            first_error_ = ec;
    }
};

/** Specialization for void `results` children (no payload storage). */
template<>
struct when_all_homogeneous_state<std::tuple<>>
{
    when_all_core core_;
    std::unique_ptr<continuation[]> runner_handles_;

    std::atomic<bool> has_error_{false};
    std::error_code first_error_;

    explicit when_all_homogeneous_state(std::size_t count)
        : core_(count)
        , runner_handles_(std::make_unique<continuation[]>(count))
    {
    }

    /** Record the first error (subsequent errors are discarded). */
    void record_error(std::error_code ec)
    {
        bool expected = false;
        if(has_error_.compare_exchange_strong(
            expected, true, std::memory_order_relaxed))
            first_error_ = ec;
    }
};

/** Wrapper coroutine that intercepts task completion for when_all.

    Parameterized on StateType to work with both heterogeneous (variadic)
    and homogeneous (range) state types. All state types expose their
    shared members through a `core_` member of type when_all_core.

    @tparam StateType The state type (when_all_state or when_all_homogeneous_state).
*/
template<typename StateType>
struct when_all_runner
{
    struct promise_type
        : detail::process_frame_mixin
    {
        StateType* state_ = nullptr;
        std::size_t index_ = 0;
        await_context env_;
        // Owns the branch's segment.
        chain_owner segment_;
        bool done_ = false;

        ~promise_type()
        {
            if(!done_ && state_)
                state_->core_.lose_branch();
        }

        when_all_runner get_return_object() noexcept
        {
            return when_all_runner(
                std::coroutine_handle<promise_type>::from_promise(*this));
        }

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
                    auto& core = p_->state_->core_;
                    auto* counter = &core.remaining_count_;
                    auto* lost = &core.lost_;
                    auto* caller_env = core.caller_env_;
                    auto& cont = core.continuation_;

                    p_->done_ = true;
                    // Resumed to unwind its abandoned branch: lost, not
                    // finished. Stop is requested while this branch
                    // still holds its share.
                    if(p_->segment_.abandoned())
                    {
                        lost->store(true, std::memory_order_relaxed);
                        core.stop_source_.request_stop();
                    }
                    h.destroy();

                    auto remaining = counter->fetch_sub(1, std::memory_order_acq_rel);
                    if(remaining != 1)
                        return detail::symmetric_transfer(std::noop_coroutine());
                    if(lost->load(std::memory_order_relaxed))
                    {
                        auto parent = cont;
                        capy::abandon(parent);
                        return detail::symmetric_transfer(std::noop_coroutine());
                    }
                    return detail::symmetric_transfer(caller_env->executor.dispatch(cont));
                }
                void await_resume() const noexcept {} // LCOV_EXCL_LINE final_suspend awaiter, never resumed
            };
            return awaiter{this};
        }

        void return_void() noexcept {}

        void unhandled_exception() noexcept
        {
            state_->core_.capture_exception(std::current_exception());
            state_->core_.stop_source_.request_stop();
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
            if constexpr (ContextAwaitable<A>)
            {
                return transform_awaiter<Awaitable>{
                    std::forward<Awaitable>(a), this};
            }
            else
            {
                static_assert(sizeof(A) == 0, "requires ContextAwaitable");
            }
        }
    };

    std::coroutine_handle<promise_type> h_;

    explicit when_all_runner(std::coroutine_handle<promise_type> h) noexcept
        : h_(h)
    {
    }

    // Enable move for all clang versions - some versions need it
    when_all_runner(when_all_runner&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    when_all_runner(when_all_runner const&) = delete;
    when_all_runner& operator=(when_all_runner const&) = delete;
    when_all_runner& operator=(when_all_runner&&) = delete;

    auto release() noexcept
    {
        return std::exchange(h_, nullptr);
    }
};

/** Create a `results`-aware runner for a single awaitable (range path).

    Checks the error code, records errors and requests stop on failure,
    or extracts the payload on success.
*/
template<ContextAwaitable Awaitable, typename StateType>
when_all_runner<StateType>
make_when_all_homogeneous_runner(Awaitable* inner, StateType* state, std::size_t index)
{
    // Taken from the join's storage only once running, into a local:
    // a runner destroyed unstarted owns no branch, and one destroyed
    // unfinished destroys it before its promise reports the loss.
    Awaitable a(std::move(*inner));
    auto r = co_await abandonable<Awaitable>{a};
    if(!r)
        co_return;
    auto& result = *r;

    if(std::get<0>(result))
    {
        state->record_error(std::get<0>(result));
        state->core_.stop_source_.request_stop();
    }
    else
    {
        using PayloadT = results_payload_t<
            awaitable_result_t<Awaitable>>;
        if constexpr (!std::is_same_v<PayloadT, std::tuple<>>)
        {
            state->set_result(index,
                extract_results_payload(std::move(result)));
        }
    }
}

/** Create a runner for `results` children that requests stop on ec. */
template<std::size_t Index, ContextAwaitable Awaitable, typename... Ts>
when_all_runner<when_all_state<Ts...>>
make_when_all_results_runner(Awaitable* inner, when_all_state<Ts...>* state)
{
    // Taken from the join's storage only once running, into a local:
    // a runner destroyed unstarted owns no branch, and one destroyed
    // unfinished destroys it before its promise reports the loss.
    Awaitable a(std::move(*inner));
    auto r = co_await abandonable<Awaitable>{a};
    if(!r)
        co_return;
    auto ec = std::get<0>(*r);
    std::get<Index>(state->results_).set(std::move(*r));

    if(ec)
    {
        state->record_error(ec);
        state->core_.stop_source_.request_stop();
    }
}

/** Launcher that uses `results`-aware runners. */
template<ContextAwaitable... Awaitables>
class when_all_results_launcher
{
    using state_type = when_all_state<awaitable_result_t<Awaitables>...>;

    std::tuple<Awaitables...>* awaitables_;
    state_type* state_;

public:
    when_all_results_launcher(
        std::tuple<Awaitables...>* awaitables,
        state_type* state)
        : awaitables_(awaitables)
        , state_(state)
    {
    }

    bool await_ready() const noexcept
    {
        return sizeof...(Awaitables) == 0;
    }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> continuation, await_context const* caller_env)
    {
        state_->core_.continuation_.h = continuation;
        state_->core_.continuation_.owner = caller_env->owner;
        state_->core_.caller_env_ = caller_env;

        if(caller_env->stop_token.stop_possible())
        {
            state_->core_.parent_stop_callback_.emplace(
                caller_env->stop_token,
                when_all_core::stop_callback_fn{&state_->core_.stop_source_});

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
        auto runner = make_when_all_results_runner<I>(
            &std::get<I>(*awaitables_), state_);

        auto h = runner.release();
        h.promise().state_ = state_;
        h.promise().segment_.bind(h, caller_ex);
        h.promise().env_ = await_context{caller_ex, token,
            state_->core_.caller_env_->frame_allocator,
            &h.promise().segment_};

        state_->runner_handles_[I].h = std::coroutine_handle<>{h};
        state_->core_.caller_env_->executor.post(state_->runner_handles_[I]);
    }
};

/** Helper to extract a single result from state.
    This is a separate function to work around a GCC-11 ICE that occurs
    when using nested immediately-invoked lambdas with pack expansion.
*/
template<std::size_t I, typename... Ts>
auto extract_single_result(when_all_state<Ts...>& state)
{
    return std::move(std::get<I>(state.results_)).get();
}

/** Extract all results from state as a tuple.
*/
template<typename... Ts>
auto extract_results(when_all_state<Ts...>& state)
{
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        // Explicit element types: CTAD would collapse a single
        // `results` child via the tuple copy deduction guide
        return std::tuple<
            decltype(extract_single_result<Is>(state))...>(
                extract_single_result<Is>(state)...);
    }(std::index_sequence_for<Ts...>{});
}

/** Starts all homogeneous runners concurrently.

    Two-phase approach: create all runners first, then post all.
    This avoids lifetime issues if a task completes synchronously.
*/
template<typename Range>
class when_all_homogeneous_launcher
{
    using Awaitable = std::ranges::range_value_t<Range>;
    using PayloadT = results_payload_t<awaitable_result_t<Awaitable>>;

    Range* range_;
    when_all_homogeneous_state<PayloadT>* state_;

public:
    when_all_homogeneous_launcher(
        Range* range,
        when_all_homogeneous_state<PayloadT>* state)
        : range_(range)
        , state_(state)
    {
    }

    bool await_ready() const noexcept
    {
        return std::ranges::empty(*range_);
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation, await_context const* caller_env)
    {
        state_->core_.continuation_.h = continuation;
        state_->core_.continuation_.owner = caller_env->owner;
        state_->core_.caller_env_ = caller_env;

        if(caller_env->stop_token.stop_possible())
        {
            state_->core_.parent_stop_callback_.emplace(
                caller_env->stop_token,
                when_all_core::stop_callback_fn{&state_->core_.stop_source_});

            if(caller_env->stop_token.stop_requested())
                state_->core_.stop_source_.request_stop();
        }

        auto token = state_->core_.stop_source_.get_token();

        // Phase 1: Create all runners without dispatching.
        std::size_t index = 0;
        for(auto&& a : *range_)
        {
            auto runner = make_when_all_homogeneous_runner(
                &a, state_, index);

            auto h = runner.release();
            h.promise().state_ = state_;
            h.promise().index_ = index;
            h.promise().segment_.bind(h, caller_env->executor);
            h.promise().env_ = await_context{caller_env->executor, token,
                caller_env->frame_allocator, &h.promise().segment_};

            state_->runner_handles_[index].h = std::coroutine_handle<>{h};
            ++index;
        }

        // Phase 2: Post all runners. Any may complete synchronously.
        // After last post, state_ and this may be destroyed.
        auto* handles = state_->runner_handles_.get();
        std::size_t count = state_->core_.remaining_count_.load(std::memory_order_relaxed);
        for(std::size_t i = 0; i < count; ++i)
            caller_env->executor.post(handles[i]);

        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
    }
};

} // namespace detail

/** Execute a range of `results`-returning awaitables concurrently.

    Starts all awaitables simultaneously and waits for all to complete.
    On success, extracted payloads are collected in a vector preserving
    input order. The first error_code makes a stop request that every
    sibling observes, and is propagated in the outer results.
    Exceptions always beat error codes.

    @li All child awaitables run concurrently on the caller's executor.
    @li Payloads are returned as a vector in input order.
    @li First error_code wins and makes a stop request that siblings observe.
    @li Exception always beats error_code.
    @li Completes only after all children have finished.

    @par Await-effects

    Takes ownership of the range, creates one wrapper coroutine per
    element, then posts every wrapper to the caller's executor. All
    children therefore run concurrently, each awaited with the caller's
    executor and frame allocator and with a stop token owned by this
    operation.

    Awaiting an empty range throws `std::invalid_argument` before any
    child is started.

    A stop request is made on the operation's own stop token when:

    @li a child await-returns a non-zero `ec`, or
    @li a child exits via an exception, or
    @li the caller's stop token is triggered.

    Every sibling observes that request through the stop token it was
    awaited with. The request does not end the operation: the await
    completes only after every child has finished.

    @par Await-returns
    An object of type `results<std::vector<PayloadT>>` destructuring as
    `[ec, values]`, where `PayloadT` is the payload of one child's
    `results`.

    `ec` is the first non-zero `ec` await-returned by a child, in
    completion order rather than input order. The `ec` of every other
    child is discarded.

    On success, `values` holds one payload per element of the input
    range, in input order. If `ec` is set, `values` is empty: the
    payloads of the children that did succeed are discarded.

    If any child exits via an exception, the first such exception is
    rethrown instead of await-returning, even when a child also reported
    an `ec`.

    @par Await-postcondition
    Every child has finished. `ec` is success only if every child
    await-returned success. If `ec` is success, `values` holds one
    payload per input awaitable; otherwise `values` is empty.

    @par Remarks
    Supports _ContextAwaitable cancellation_.

    @par Thread Safety
    The returned task must be awaited from a single execution context.
    Child awaitables execute concurrently but complete through the caller's
    executor.

    @param awaitables Range of `results`-returning awaitables to execute
        concurrently (must not be empty).

    @return A task yielding `results<vector<PayloadT>>` where PayloadT
        is the payload extracted from each child's results.

    @throws std::invalid_argument if range is empty (thrown before
        coroutine suspends).

    @par Exception Safety
    If a child throws, the first child exception is rethrown after
    all children complete (exception beats error_code).

    @note If a branch's coroutine is abandoned before it finishes, for
        example by teardown of its execution context, `when_all`
        requests stop on the remaining branches, and the awaiting
        parent is abandoned rather than resumed once the last branch
        has finished or been abandoned.

    @par Example
    @par !example example_1


    @see ContextAwaitableRange, when_all
*/
template<ContextAwaitableRange R>
    requires detail::is_results_v<
        awaitable_result_t<std::ranges::range_value_t<R>>>
    && (!std::is_same_v<
            detail::results_payload_t<
                awaitable_result_t<std::ranges::range_value_t<R>>>,
            std::tuple<>>)
[[nodiscard]] auto when_all(R&& awaitables)
    -> task<results<std::vector<
        detail::results_payload_t<
            awaitable_result_t<std::ranges::range_value_t<R>>>>>>
{
    using Awaitable = std::ranges::range_value_t<R>;
    using PayloadT = detail::results_payload_t<
        awaitable_result_t<Awaitable>>;
    using OwnedRange = detail::owned_range_t<R>;

    auto count = std::ranges::size(awaitables);
    if(count == 0)
        throw std::invalid_argument("when_all requires at least one awaitable");

    OwnedRange owned_awaitables = detail::own_range(std::forward<R>(awaitables));

    detail::when_all_homogeneous_state<PayloadT> state(count);

    co_await detail::when_all_homogeneous_launcher<OwnedRange>(
        &owned_awaitables, &state);

    if(state.core_.first_exception_)
        std::rethrow_exception(state.core_.first_exception_);

    if(state.has_error_.load(std::memory_order_relaxed))
        co_return results<std::vector<PayloadT>>{state.first_error_, {}};

    std::vector<PayloadT> values;
    values.reserve(count);
    for(auto& opt : state.results_)
        values.push_back(std::move(*opt));

    co_return results<std::vector<PayloadT>>{std::error_code(), std::move(values)};
}

/** Execute a range of void `results`-returning awaitables concurrently.

    Starts all awaitables simultaneously and waits for all to complete.
    Since all awaitables return results<>, no payload values are
    collected. The first error_code makes a stop request that every
    sibling observes, and is propagated. Exceptions always beat error
    codes.

    @par Await-effects

    Takes ownership of the range, creates one wrapper coroutine per
    element, then posts every wrapper to the caller's executor. All
    children therefore run concurrently, each awaited with the caller's
    executor and frame allocator and with a stop token owned by this
    operation.

    Awaiting an empty range throws `std::invalid_argument` before any
    child is started.

    A stop request is made on the operation's own stop token when:

    @li a child await-returns a non-zero `ec`, or
    @li a child exits via an exception, or
    @li the caller's stop token is triggered.

    Every sibling observes that request through the stop token it was
    awaited with. The request does not end the operation: the await
    completes only after every child has finished.

    @par Await-returns
    An object of type `results<>` destructuring as `[ec]`. The children
    have no payloads, so nothing else is reported.

    `ec` is the first non-zero `ec` await-returned by a child, in
    completion order rather than input order. The `ec` of every other
    child is discarded.

    If any child exits via an exception, the first such exception is
    rethrown instead of await-returning, even when a child also reported
    an `ec`.

    @par Await-postcondition
    Every child has finished. `ec` is success only if every child
    await-returned success.

    @par Remarks
    Supports _ContextAwaitable cancellation_.

    @par Thread Safety
    The returned task must be awaited from a single execution context.
    Child awaitables execute concurrently but complete through the caller's
    executor.

    @param awaitables Range of `results<>`-returning awaitables to
        execute concurrently (must not be empty).

    @return A task yielding `results<>` whose ec is the first child
        error, or default-constructed on success.

    @throws std::invalid_argument if range is empty.

    @par Exception Safety
    If a child throws, the first child exception is rethrown after
    all children complete (exception beats error_code).

    @note If a branch's coroutine is abandoned before it finishes, for
        example by teardown of its execution context, `when_all`
        requests stop on the remaining branches, and the awaiting
        parent is abandoned rather than resumed once the last branch
        has finished or been abandoned.

    @par Example
    @par !example example_2


    @see ContextAwaitableRange, when_all
*/
template<ContextAwaitableRange R>
    requires detail::is_results_v<
        awaitable_result_t<std::ranges::range_value_t<R>>>
    && std::is_same_v<
            detail::results_payload_t<
                awaitable_result_t<std::ranges::range_value_t<R>>>,
            std::tuple<>>
[[nodiscard]] auto when_all(R&& awaitables) -> task<results<>>
{
    using OwnedRange = detail::owned_range_t<R>;

    auto count = std::ranges::size(awaitables);
    if(count == 0)
        throw std::invalid_argument("when_all requires at least one awaitable");

    OwnedRange owned_awaitables = detail::own_range(std::forward<R>(awaitables));

    detail::when_all_homogeneous_state<std::tuple<>> state(count);

    co_await detail::when_all_homogeneous_launcher<OwnedRange>(
        &owned_awaitables, &state);

    if(state.core_.first_exception_)
        std::rethrow_exception(state.core_.first_exception_);

    if(state.has_error_.load(std::memory_order_relaxed))
        co_return results<>{state.first_error_};

    co_return results<>{};
}

/** Execute `results`-returning awaitables concurrently, inspecting error codes.

    Overload selected when all children return results<Ts...>.
    The error_code is lifted out of each child into a single outer
    results. On success all values are returned; on failure the
    first error_code wins.

    @par Await-effects

    Creates and posts one wrapper coroutine per argument to the caller's
    executor, in argument order. All children therefore run concurrently,
    each awaited with the caller's executor and frame allocator and with
    a stop token owned by this operation. The overload requires at least
    one awaitable, so there is no empty case.

    A stop request is made on the operation's own stop token when:

    @li a child await-returns a non-zero `ec`, or
    @li a child exits via an exception, or
    @li the caller's stop token is triggered.

    Every sibling observes that request through the stop token it was
    awaited with. The request does not end the operation: the await
    completes only after every child has finished.

    @par Await-returns
    An object of type `results<P1, ..., Pn>` destructuring as
    `[ec, v1, ..., vn]`, where `Pi` is the payload of the i-th child's
    `results`.

    `ec` is the first non-zero `ec` await-returned by a child, in
    completion order rather than argument order. The `ec` of every other
    child is discarded.

    Each `vi` is the payload the i-th child itself await-returned, even
    when that child or a sibling reported an `ec`. A failed child
    therefore still contributes whatever payload it produced. This
    differs from the range overloads, which discard all payloads once any
    child fails.

    If any child exits via an exception, the first such exception is
    rethrown instead of await-returning, even when a child also reported
    an `ec`.

    @par Await-postcondition
    Every child has finished. Each `vi` holds the i-th child's payload,
    and `ec` is success only if every child await-returned success.

    @par Remarks
    Supports _ContextAwaitable cancellation_.

    @par Thread Safety
    The returned task must be awaited from a single execution context.
    Child awaitables execute concurrently but complete through the caller's
    executor.

    @par Exception Safety
    If a child throws, the first child exception is rethrown after
    all children complete (exception beats error_code).

    @note If a branch's coroutine is abandoned before it finishes, for
        example by teardown of its execution context, `when_all`
        requests stop on the remaining branches, and the awaiting
        parent is abandoned rather than resumed once the last branch
        has finished or been abandoned.

    @param awaitables One or more awaitables each returning
        results<Ts...>.

    @return A task yielding `results<R1, R2, ..., Rn>` where each Ri
        follows the payload flattening rules.
*/
template<ContextAwaitable... As>
    requires (sizeof...(As) > 0)
          && detail::all_results_awaitables<As...>
[[nodiscard]] auto when_all(As... awaitables)
    -> task<results<
        detail::results_payload_t<awaitable_result_t<As>>...>>
{
    using result_type = results<
        detail::results_payload_t<awaitable_result_t<As>>...>;

    detail::when_all_state<awaitable_result_t<As>...> state;
    std::tuple<As...> awaitable_tuple(std::move(awaitables)...);

    co_await detail::when_all_results_launcher<As...>(&awaitable_tuple, &state);

    // Exception always wins over error_code
    if(state.core_.first_exception_)
        std::rethrow_exception(state.core_.first_exception_);

    auto r = detail::build_when_all_results<result_type>(
        detail::extract_results(state));
    if(state.has_error_.load(std::memory_order_relaxed))
        std::get<0>(r) = state.first_error_;
    co_return r;
}

} // namespace capy
} // namespace boost

#endif
