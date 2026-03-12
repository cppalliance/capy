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
#include <boost/capy/detail/void_to_monostate.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <coroutine>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
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

/** Specialization for void tasks - returns monostate to preserve index mapping.
*/
template<>
struct result_holder<void>
{
    std::monostate get() && { return {}; }
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

    std::coroutine_handle<> continuation_;
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
    std::array<std::coroutine_handle<>, task_count> runner_handles_{};

    when_all_state()
        : core_(task_count)
    {
    }
};

/** Shared state for homogeneous when_all (range overload).

    Stores all results in a vector indexed by task position.

    @tparam T The common result type of all tasks.
*/
template<typename T>
struct when_all_homogeneous_state
{
    when_all_core core_;
    std::vector<std::optional<T>> results_;
    std::vector<std::coroutine_handle<>> runner_handles_;

    explicit when_all_homogeneous_state(std::size_t count)
        : core_(count)
        , results_(count)
        , runner_handles_(count)
    {
    }

    void set_result(std::size_t index, T value)
    {
        results_[index].emplace(std::move(value));
    }
};

/** Specialization for void tasks (no result storage). */
template<>
struct when_all_homogeneous_state<void>
{
    when_all_core core_;
    std::vector<std::coroutine_handle<>> runner_handles_;

    explicit when_all_homogeneous_state(std::size_t count)
        : core_(count)
        , runner_handles_(count)
    {
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
                    auto cont = core.continuation_;

                    h.destroy();

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

        void unhandled_exception()
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

/** Create a runner coroutine for a single awaitable (variadic path).

    Uses compile-time index for tuple-based result storage.
*/
template<std::size_t Index, IoAwaitable Awaitable, typename... Ts>
when_all_runner<when_all_state<Ts...>>
make_when_all_runner(Awaitable inner, when_all_state<Ts...>* state)
{
    using T = awaitable_result_t<Awaitable>;
    if constexpr (std::is_void_v<T>)
    {
        co_await std::move(inner);
    }
    else
    {
        std::get<Index>(state->results_).set(co_await std::move(inner));
    }
}

/** Create a runner coroutine for a single awaitable (range path).

    Uses runtime index for vector-based result storage.
*/
template<IoAwaitable Awaitable, typename StateType>
when_all_runner<StateType>
make_when_all_homogeneous_runner(Awaitable inner, StateType* state, std::size_t index)
{
    using T = awaitable_result_t<Awaitable>;
    if constexpr (std::is_void_v<T>)
    {
        co_await std::move(inner);
    }
    else
    {
        state->set_result(index, co_await std::move(inner));
    }
}

/** Internal awaitable that launches all variadic runner coroutines.

    CRITICAL: If the last task finishes synchronously then the parent
    coroutine resumes, destroying its frame, and destroying this object
    prior to the completion of await_suspend. Therefore, await_suspend
    must ensure `this` cannot be referenced after calling `launch_one`
    for the last time.
*/
template<IoAwaitable... Awaitables>
class when_all_launcher
{
    using state_type = when_all_state<awaitable_result_t<Awaitables>...>;

    std::tuple<Awaitables...>* awaitables_;
    state_type* state_;

public:
    when_all_launcher(
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

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation, io_env const* caller_env)
    {
        state_->core_.continuation_ = continuation;
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
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (..., launch_one<Is>(caller_env->executor, token));
        }(std::index_sequence_for<Awaitables...>{});

        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
    }

private:
    template<std::size_t I>
    void launch_one(executor_ref caller_ex, std::stop_token token)
    {
        auto runner = make_when_all_runner<I>(
            std::move(std::get<I>(*awaitables_)), state_);

        auto h = runner.release();
        h.promise().state_ = state_;
        h.promise().env_ = io_env{caller_ex, token, state_->core_.caller_env_->frame_allocator};

        std::coroutine_handle<> ch{h};
        state_->runner_handles_[I] = ch;
        state_->core_.caller_env_->executor.post(ch);
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
        return std::tuple(extract_single_result<Is>(state)...);
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
    using T = awaitable_result_t<Awaitable>;

    Range* range_;
    when_all_homogeneous_state<T>* state_;

public:
    when_all_homogeneous_launcher(
        Range* range,
        when_all_homogeneous_state<T>* state)
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
        state_->core_.continuation_ = continuation;
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

            state_->runner_handles_[index] = std::coroutine_handle<>{h};
            ++index;
        }

        // Phase 2: Post all runners. Any may complete synchronously.
        // After last post, state_ and this may be destroyed.
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

/** Compute the when_all result tuple type.

    Void-returning tasks contribute std::monostate to preserve the
    task-index-to-result-index mapping, matching when_any's approach.

    Example: when_all_result_t<int, void, string> = std::tuple<int, std::monostate, string>
    Example: when_all_result_t<void, void> = std::tuple<std::monostate, std::monostate>
*/
template<typename... Ts>
using when_all_result_t = std::tuple<void_to_monostate_t<Ts>...>;

/** Execute multiple awaitables concurrently and collect their results.

    Launches all awaitables simultaneously and waits for all to complete
    before returning. Results are collected in input order. If any
    awaitable throws, cancellation is requested for siblings and the first
    exception is rethrown after all awaitables complete.

    @li All child awaitables run concurrently on the caller's executor
    @li Results are returned as a tuple in input order
    @li Void-returning awaitables contribute std::monostate to the
        result tuple, preserving the task-index-to-result-index mapping
    @li First exception wins; subsequent exceptions are discarded
    @li Stop is requested for siblings on first error
    @li Completes only after all children have finished

    @par Thread Safety
    The returned task must be awaited from a single execution context.
    Child awaitables execute concurrently but complete through the caller's
    executor.

    @param awaitables The awaitables to execute concurrently. Each must
        satisfy @ref IoAwaitable and is consumed (moved-from) when
        `when_all` is awaited.

    @return A task yielding a tuple of results in input order. Void tasks
        contribute std::monostate to preserve index correspondence.

    @par Example

    @code
    task<> example()
    {
        // Concurrent fetch, results collected in order
        auto [user, posts] = co_await when_all(
            fetch_user( id ),      // task<User>
            fetch_posts( id )      // task<std::vector<Post>>
        );

        // Void awaitables contribute monostate
        auto [a, _, b] = co_await when_all(
            fetch_int(),           // task<int>
            log_event( "start" ),  // task<void>  → monostate
            fetch_str()            // task<string>
        );
        // a is int, _ is monostate, b is string
    }
    @endcode

    @see IoAwaitable, task
*/
template<IoAwaitable... As>
[[nodiscard]] auto when_all(As... awaitables)
    -> task<when_all_result_t<awaitable_result_t<As>...>>
{
    // State is stored in the coroutine frame, using the frame allocator
    detail::when_all_state<awaitable_result_t<As>...> state;

    // Store awaitables in the frame
    std::tuple<As...> awaitable_tuple(std::move(awaitables)...);

    // Launch all awaitables and wait for completion
    co_await detail::when_all_launcher<As...>(&awaitable_tuple, &state);

    // Propagate first exception if any.
    // Safe without explicit acquire: capture_exception() is sequenced-before
    // signal_completion()'s acq_rel fetch_sub, which synchronizes-with the
    // last task's decrement that resumes this coroutine.
    if(state.core_.first_exception_)
        std::rethrow_exception(state.core_.first_exception_);

    co_return detail::extract_results(state);
}

/** Execute a range of awaitables concurrently and collect their results.

    Launches all awaitables in the range simultaneously and waits for all
    to complete. Results are collected in a vector preserving input order.
    If any awaitable throws, cancellation is requested for siblings and
    the first exception is rethrown after all awaitables complete.

    @li All child awaitables run concurrently on the caller's executor
    @li Results are returned as a vector in input order
    @li First exception wins; subsequent exceptions are discarded
    @li Stop is requested for siblings on first error
    @li Completes only after all children have finished

    @par Thread Safety
    The returned task must be awaited from a single execution context.
    Child awaitables execute concurrently but complete through the caller's
    executor.

    @param awaitables Range of awaitables to execute concurrently (must
        not be empty). Each element must satisfy @ref IoAwaitable and is
        consumed (moved-from) when `when_all` is awaited.

    @return A task yielding a vector where each element is the result of
        the corresponding awaitable, in input order.

    @throws std::invalid_argument if range is empty (thrown before
        coroutine suspends).
    @throws Rethrows the first child exception after all children
        complete.

    @par Example
    @code
    task<void> example()
    {
        std::vector<task<Response>> requests;
        for (auto const& url : urls)
            requests.push_back(fetch(url));

        auto responses = co_await when_all(std::move(requests));
    }
    @endcode

    @see IoAwaitableRange, when_all
*/
template<IoAwaitableRange R>
    requires (!std::is_void_v<awaitable_result_t<std::ranges::range_value_t<R>>>)
[[nodiscard]] auto when_all(R&& awaitables)
    -> task<std::vector<awaitable_result_t<std::ranges::range_value_t<R>>>>
{
    using Awaitable = std::ranges::range_value_t<R>;
    using T = awaitable_result_t<Awaitable>;
    using OwnedRange = std::remove_cvref_t<R>;

    auto count = std::ranges::size(awaitables);
    if(count == 0)
        throw std::invalid_argument("when_all requires at least one awaitable");

    OwnedRange owned_awaitables = std::forward<R>(awaitables);

    detail::when_all_homogeneous_state<T> state(count);

    co_await detail::when_all_homogeneous_launcher<OwnedRange>(
        &owned_awaitables, &state);

    if(state.core_.first_exception_)
        std::rethrow_exception(state.core_.first_exception_);

    std::vector<T> results;
    results.reserve(count);
    for(auto& opt : state.results_)
        results.push_back(std::move(*opt));

    co_return results;
}

/** Execute a range of void awaitables concurrently.

    Launches all awaitables in the range simultaneously and waits for all
    to complete. Since all awaitables return void, no results are collected.
    If any awaitable throws, cancellation is requested for siblings and
    the first exception is rethrown after all awaitables complete.

    @li All child awaitables run concurrently on the caller's executor
    @li First exception wins; subsequent exceptions are discarded
    @li Stop is requested for siblings on first error
    @li Completes only after all children have finished

    @par Thread Safety
    The returned task must be awaited from a single execution context.
    Child awaitables execute concurrently but complete through the caller's
    executor.

    @param awaitables Range of void awaitables to execute concurrently
        (must not be empty).

    @throws std::invalid_argument if range is empty (thrown before
        coroutine suspends).
    @throws Rethrows the first child exception after all children
        complete.

    @par Example
    @code
    task<void> example()
    {
        std::vector<task<void>> jobs;
        for (int i = 0; i < n; ++i)
            jobs.push_back(process(i));

        co_await when_all(std::move(jobs));
    }
    @endcode

    @see IoAwaitableRange, when_all
*/
template<IoAwaitableRange R>
    requires std::is_void_v<awaitable_result_t<std::ranges::range_value_t<R>>>
[[nodiscard]] auto when_all(R&& awaitables) -> task<void>
{
    using OwnedRange = std::remove_cvref_t<R>;

    auto count = std::ranges::size(awaitables);
    if(count == 0)
        throw std::invalid_argument("when_all requires at least one awaitable");

    OwnedRange owned_awaitables = std::forward<R>(awaitables);

    detail::when_all_homogeneous_state<void> state(count);

    co_await detail::when_all_homogeneous_launcher<OwnedRange>(
        &owned_awaitables, &state);

    if(state.core_.first_exception_)
        std::rethrow_exception(state.core_.first_exception_);
}

} // namespace capy
} // namespace boost

#endif
