//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_WHEN_ALL_HPP
#define BOOST_CAPY_WHEN_ALL_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/io_result_combinators.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <coroutine>
#include <boost/capy/ex/frame_alloc_mixin.hpp>
#include <boost/capy/ex/io_env.hpp>
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
        void operator()() const { source_->request_stop(); }
    };
    using stop_callback_t = std::stop_callback<stop_callback_fn>;
    std::optional<stop_callback_t> parent_stop_callback_;

    continuation continuation_;
    io_env const* caller_env_ = nullptr;

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

    Stores extracted io_result payloads in a vector indexed by task
    position. Tracks the first error_code for error propagation.

    @tparam T The payload type extracted from io_result.
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

/** Specialization for void io_result children (no payload storage). */
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
struct BOOST_CAPY_CORO_DESTROY_WHEN_COMPLETE when_all_runner
{
    struct promise_type
        : frame_alloc_mixin
    {
        StateType* state_ = nullptr;
        std::size_t index_ = 0;
        io_env env_;

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

/** Create an io_result-aware runner for a single awaitable (range path).

    Checks the error code, records errors and requests stop on failure,
    or extracts the payload on success.
*/
template<IoAwaitable Awaitable, typename StateType>
when_all_runner<StateType>
make_when_all_homogeneous_runner(Awaitable inner, StateType* state, std::size_t index)
{
    auto result = co_await std::move(inner);

    if(std::get<0>(result))
    {
        state->record_error(std::get<0>(result));
        state->core_.stop_source_.request_stop();
    }
    else
    {
        using PayloadT = io_result_payload_t<
            awaitable_result_t<Awaitable>>;
        if constexpr (!std::is_same_v<PayloadT, std::tuple<>>)
        {
            state->set_result(index,
                extract_io_payload(std::move(result)));
        }
    }
}

/** Create a runner for io_result children that requests stop on ec. */
template<std::size_t Index, IoAwaitable Awaitable, typename... Ts>
when_all_runner<when_all_state<Ts...>>
make_when_all_io_runner(Awaitable inner, when_all_state<Ts...>* state)
{
    auto result = co_await std::move(inner);
    auto ec = std::get<0>(result);
    std::get<Index>(state->results_).set(std::move(result));

    if(ec)
    {
        state->record_error(ec);
        state->core_.stop_source_.request_stop();
    }
}

/** Launcher that uses io_result-aware runners. */
template<IoAwaitable... Awaitables>
class when_all_io_launcher
{
    using state_type = when_all_state<awaitable_result_t<Awaitables>...>;

    std::tuple<Awaitables...>* awaitables_;
    state_type* state_;

public:
    when_all_io_launcher(
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
        std::coroutine_handle<> continuation, io_env const* caller_env)
    {
        state_->core_.continuation_.h = continuation;
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
        auto runner = make_when_all_io_runner<I>(
            std::move(std::get<I>(*awaitables_)), state_);

        auto h = runner.release();
        h.promise().state_ = state_;
        h.promise().env_ = io_env{caller_ex, token,
            state_->core_.caller_env_->frame_allocator};

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
        // io_result child via the tuple copy deduction guide
        return std::tuple<
            decltype(extract_single_result<Is>(state))...>(
                extract_single_result<Is>(state)...);
    }(std::index_sequence_for<Ts...>{});
}

/** Launches all homogeneous runners concurrently.

    Two-phase approach: create all runners first, then post all.
    This avoids lifetime issues if a task completes synchronously.
*/
template<typename Range>
class when_all_homogeneous_launcher
{
    using Awaitable = std::ranges::range_value_t<Range>;
    using PayloadT = io_result_payload_t<awaitable_result_t<Awaitable>>;

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

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation, io_env const* caller_env)
    {
        state_->core_.continuation_.h = continuation;
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
                std::move(a), state_, index);

            auto h = runner.release();
            h.promise().state_ = state_;
            h.promise().index_ = index;
            h.promise().env_ = io_env{caller_env->executor, token, caller_env->frame_allocator};

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

/** Execute a range of io_result-returning awaitables concurrently.

    Launches all awaitables simultaneously and waits for all to complete.
    On success, extracted payloads are collected in a vector preserving
    input order. The first error_code cancels siblings and is propagated
    in the outer io_result. Exceptions always beat error codes.

    @li All child awaitables run concurrently on the caller's executor
    @li Payloads are returned as a vector in input order
    @li First error_code wins and cancels siblings
    @li Exception always beats error_code
    @li Completes only after all children have finished

    @par Thread Safety
    The returned task must be awaited from a single execution context.
    Child awaitables execute concurrently but complete through the caller's
    executor.

    @param awaitables Range of io_result-returning awaitables to execute
        concurrently (must not be empty).

    @return A task yielding io_result<vector<PayloadT>> where PayloadT
        is the payload extracted from each child's io_result.

    @throws std::invalid_argument if range is empty (thrown before
        coroutine suspends).
    @throws Rethrows the first child exception after all children
        complete (exception beats error_code).

    @par Example
    @code
    task<void> example()
    {
        std::vector<io_task<size_t>> reads;
        for (auto& buf : buffers)
            reads.push_back(stream.read_some(buf));

        auto [ec, counts] = co_await when_all(std::move(reads));
        if (ec) { // handle error
        }
    }
    @endcode

    @see IoAwaitableRange, when_all
*/
template<IoAwaitableRange R>
    requires detail::is_io_result_v<
        awaitable_result_t<std::ranges::range_value_t<R>>>
    && (!std::is_same_v<
            detail::io_result_payload_t<
                awaitable_result_t<std::ranges::range_value_t<R>>>,
            std::tuple<>>)
[[nodiscard]] auto when_all(R&& awaitables)
    -> task<io_result<std::vector<
        detail::io_result_payload_t<
            awaitable_result_t<std::ranges::range_value_t<R>>>>>>
{
    using Awaitable = std::ranges::range_value_t<R>;
    using PayloadT = detail::io_result_payload_t<
        awaitable_result_t<Awaitable>>;
    using OwnedRange = std::remove_cvref_t<R>;

    auto count = std::ranges::size(awaitables);
    if(count == 0)
        throw std::invalid_argument("when_all requires at least one awaitable");

    OwnedRange owned_awaitables = std::forward<R>(awaitables);

    detail::when_all_homogeneous_state<PayloadT> state(count);

    co_await detail::when_all_homogeneous_launcher<OwnedRange>(
        &owned_awaitables, &state);

    if(state.core_.first_exception_)
        std::rethrow_exception(state.core_.first_exception_);

    if(state.has_error_.load(std::memory_order_relaxed))
        co_return io_result<std::vector<PayloadT>>{state.first_error_, {}};

    std::vector<PayloadT> results;
    results.reserve(count);
    for(auto& opt : state.results_)
        results.push_back(std::move(*opt));

    co_return io_result<std::vector<PayloadT>>{std::error_code(), std::move(results)};
}

/** Execute a range of void io_result-returning awaitables concurrently.

    Launches all awaitables simultaneously and waits for all to complete.
    Since all awaitables return io_result<>, no payload values are
    collected. The first error_code cancels siblings and is propagated.
    Exceptions always beat error codes.

    @param awaitables Range of io_result<>-returning awaitables to
        execute concurrently (must not be empty).

    @return A task yielding io_result<> whose ec is the first child
        error, or default-constructed on success.

    @throws std::invalid_argument if range is empty.
    @throws Rethrows the first child exception after all children
        complete (exception beats error_code).

    @par Example
    @code
    task<void> example()
    {
        std::vector<io_task<>> jobs;
        for (int i = 0; i < n; ++i)
            jobs.push_back(process(i));

        auto [ec] = co_await when_all(std::move(jobs));
    }
    @endcode

    @see IoAwaitableRange, when_all
*/
template<IoAwaitableRange R>
    requires detail::is_io_result_v<
        awaitable_result_t<std::ranges::range_value_t<R>>>
    && std::is_same_v<
            detail::io_result_payload_t<
                awaitable_result_t<std::ranges::range_value_t<R>>>,
            std::tuple<>>
[[nodiscard]] auto when_all(R&& awaitables) -> task<io_result<>>
{
    using OwnedRange = std::remove_cvref_t<R>;

    auto count = std::ranges::size(awaitables);
    if(count == 0)
        throw std::invalid_argument("when_all requires at least one awaitable");

    OwnedRange owned_awaitables = std::forward<R>(awaitables);

    detail::when_all_homogeneous_state<std::tuple<>> state(count);

    co_await detail::when_all_homogeneous_launcher<OwnedRange>(
        &owned_awaitables, &state);

    if(state.core_.first_exception_)
        std::rethrow_exception(state.core_.first_exception_);

    if(state.has_error_.load(std::memory_order_relaxed))
        co_return io_result<>{state.first_error_};

    co_return io_result<>{};
}

/** Execute io_result-returning awaitables concurrently, inspecting error codes.

    Overload selected when all children return io_result<Ts...>.
    The error_code is lifted out of each child into a single outer
    io_result. On success all values are returned; on failure the
    first error_code wins.

    @par Exception Safety
    Exception always beats error_code. If any child throws, the
    exception is rethrown regardless of error_code results.

    @param awaitables One or more awaitables each returning
        io_result<Ts...>.

    @return A task yielding io_result<R1, R2, ..., Rn> where each Ri
        follows the payload flattening rules.

    @throws Rethrows the first child exception after all children
        complete (exception beats error_code).
*/
template<IoAwaitable... As>
    requires (sizeof...(As) > 0)
          && detail::all_io_result_awaitables<As...>
[[nodiscard]] auto when_all(As... awaitables)
    -> task<io_result<
        detail::io_result_payload_t<awaitable_result_t<As>>...>>
{
    using result_type = io_result<
        detail::io_result_payload_t<awaitable_result_t<As>>...>;

    detail::when_all_state<awaitable_result_t<As>...> state;
    std::tuple<As...> awaitable_tuple(std::move(awaitables)...);

    co_await detail::when_all_io_launcher<As...>(&awaitable_tuple, &state);

    // Exception always wins over error_code
    if(state.core_.first_exception_)
        std::rethrow_exception(state.core_.first_exception_);

    auto r = detail::build_when_all_io_result<result_type>(
        detail::extract_results(state));
    if(state.has_error_.load(std::memory_order_relaxed))
        std::get<0>(r) = state.first_error_;
    co_return r;
}

} // namespace capy
} // namespace boost

#endif
