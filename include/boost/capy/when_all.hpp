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
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/any_coro.hpp>
#include <boost/capy/ex/any_executor_ref.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/task.hpp>

#include <array>
#include <atomic>
#include <exception>
#include <optional>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

namespace detail {

/** Type trait to filter void types from a tuple.

    Void-returning tasks do not contribute a value to the result tuple.
    This trait computes the filtered result type.

    Example: filter_void_tuple_t<int, void, string> = tuple<int, string>
*/
template<typename T>
using wrap_non_void_t = std::conditional_t<std::is_void_v<T>, std::tuple<>, std::tuple<T>>;

template<typename... Ts>
using filter_void_tuple_t = decltype(std::tuple_cat(std::declval<wrap_non_void_t<Ts>>()...));

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

/** Specialization for void tasks - no value storage needed.
*/
template<>
struct result_holder<void>
{
};

/** Shared state for when_all operation.

    @tparam Ts The result types of the tasks.
*/
template<typename... Ts>
struct when_all_state
{
    static constexpr std::size_t task_count = sizeof...(Ts);

    // Completion tracking - when_all waits for all children
    std::atomic<std::size_t> remaining_count_;

    // Result storage in input order
    std::tuple<result_holder<Ts>...> results_;

    // Runner handles - destroyed in await_resume while allocator is valid
    std::array<any_coro, task_count> runner_handles_{};

    // Exception storage - first error wins, others discarded
    std::atomic<bool> has_exception_{false};
    std::exception_ptr first_exception_;

    // Stop propagation - on error, request stop for siblings
    std::stop_source stop_source_;

    // Connects parent's stop_token to our stop_source
    struct stop_callback_fn
    {
        std::stop_source* source_;
        void operator()() const { source_->request_stop(); }
    };
    using stop_callback_t = std::stop_callback<stop_callback_fn>;
    std::optional<stop_callback_t> parent_stop_callback_;

    // Parent resumption
    any_coro continuation_;
    any_executor_ref caller_ex_;

    when_all_state()
        : remaining_count_(task_count)
    {
    }

    ~when_all_state()
    {
        for(auto h : runner_handles_)
            if(h)
                h.destroy();
    }

    /** Capture an exception (first one wins).
    */
    void capture_exception(std::exception_ptr ep)
    {
        bool expected = false;
        if(has_exception_.compare_exchange_strong(
            expected, true, std::memory_order_relaxed))
            first_exception_ = ep;
    }

    /** Signal that a task has completed.

        The last child to complete triggers resumption of the parent.
    */
    any_coro signal_completion()
    {
        auto remaining = remaining_count_.fetch_sub(1, std::memory_order_acq_rel);
        if(remaining == 1)
            return caller_ex_.dispatch(continuation_);
        return std::noop_coroutine();
    }

};

/** Wrapper coroutine that intercepts task completion.

    This runner awaits its assigned task and stores the result in
    the shared state, or captures the exception and requests stop.
*/
template<typename T, typename... Ts>
struct when_all_runner
{
    struct promise_type : frame_allocating_base
    {
        when_all_state<Ts...>* state_ = nullptr;
        any_executor_ref ex_;
        std::stop_token stop_token_;

        when_all_runner get_return_object()
        {
            return when_all_runner(std::coroutine_handle<promise_type>::from_promise(*this));
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

                bool await_ready() const noexcept
                {
                    return false;
                }

                any_coro await_suspend(any_coro) noexcept
                {
                    // Signal completion; last task resumes parent
                    return p_->state_->signal_completion();
                }

                void await_resume() const noexcept
                {
                }
            };
            return awaiter{this};
        }

        void return_void()
        {
        }

        void unhandled_exception()
        {
            state_->capture_exception(std::current_exception());
            // Request stop for sibling tasks
            state_->stop_source_.request_stop();
        }

        template<class Awaitable>
        struct transform_awaiter
        {
            std::decay_t<Awaitable> a_;
            promise_type* p_;

            bool await_ready()
            {
                return a_.await_ready();
            }

            auto await_resume()
            {
                return a_.await_resume();
            }

            template<class Promise>
            auto await_suspend(std::coroutine_handle<Promise> h)
            {
                return a_.await_suspend(h, p_->ex_, p_->stop_token_);
            }
        };

        template<class Awaitable>
        auto await_transform(Awaitable&& a)
        {
            using A = std::decay_t<Awaitable>;
            if constexpr (IoAwaitable<A, any_executor_ref>)
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

    explicit when_all_runner(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }

#if defined(__clang__) && __clang_major__ == 14 && !defined(__apple_build_version__)
    // Clang 14 has a bug where it calls the move constructor for coroutine
    // return objects even though they should be constructed in-place via RVO.
    // This happens when returning a non-movable type from a coroutine.
    when_all_runner(when_all_runner&& other) noexcept : h_(std::exchange(other.h_, nullptr)) {}
#endif

    // Non-copyable, non-movable - release() is always called immediately
    when_all_runner(when_all_runner const&) = delete;
    when_all_runner& operator=(when_all_runner const&) = delete;

#if !defined(__clang__) || __clang_major__ != 14 || defined(__apple_build_version__)
    when_all_runner(when_all_runner&&) = delete;
#endif

    when_all_runner& operator=(when_all_runner&&) = delete;

    auto release() noexcept
    {
        return std::exchange(h_, nullptr);
    }
};

/** Create a runner coroutine for a single task.

    Task is passed directly to ensure proper coroutine frame storage.
*/
template<std::size_t Index, typename T, typename... Ts>
when_all_runner<T, Ts...>
make_when_all_runner(task<T> inner, when_all_state<Ts...>* state)
{
    if constexpr (std::is_void_v<T>)
        co_await std::move(inner);
    else
        std::get<Index>(state->results_).set(co_await std::move(inner));
}

/** Internal awaitable that launches all runner coroutines and waits.

    This awaitable is used inside the when_all coroutine to handle
    the concurrent execution of child tasks.
*/
template<typename... Ts>
class when_all_launcher
{
    std::tuple<task<Ts>...>* tasks_;
    when_all_state<Ts...>* state_;

public:
    when_all_launcher(
        std::tuple<task<Ts>...>* tasks,
        when_all_state<Ts...>* state)
        : tasks_(tasks)
        , state_(state)
    {
    }

    bool await_ready() const noexcept
    {
        return sizeof...(Ts) == 0;
    }

    template<typename Ex>
    any_coro await_suspend(any_coro continuation, Ex const& caller_ex, std::stop_token parent_token = {})
    {
        state_->continuation_ = continuation;
        state_->caller_ex_ = caller_ex;

        // Forward parent's stop requests to children
        if(parent_token.stop_possible())
        {
            state_->parent_stop_callback_.emplace(
                parent_token,
                typename when_all_state<Ts...>::stop_callback_fn{&state_->stop_source_});

            if(parent_token.stop_requested())
                state_->stop_source_.request_stop();
        }

        // Launch all tasks concurrently
        auto token = state_->stop_source_.get_token();
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (..., launch_one<Is>(caller_ex, token));
        }(std::index_sequence_for<Ts...>{});

        // Let signal_completion() handle resumption
        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
        // Results are extracted by the when_all coroutine from state
    }

private:
    template<std::size_t I, typename Ex>
    void launch_one(Ex const& caller_ex, std::stop_token token)
    {
        auto runner = make_when_all_runner<I>(
            std::move(std::get<I>(*tasks_)), state_);

        auto h = runner.release();
        h.promise().state_ = state_;
        h.promise().ex_ = caller_ex;
        h.promise().stop_token_ = token;

        any_coro ch{h};
        state_->runner_handles_[I] = ch;
        caller_ex.dispatch(ch).resume();
    }
};

/** Compute the result type for when_all.

    Returns void when all tasks are void (P2300 aligned),
    otherwise returns a tuple with void types filtered out.
*/
template<typename... Ts>
using when_all_result_t = std::conditional_t<
    std::is_same_v<filter_void_tuple_t<Ts...>, std::tuple<>>,
    void,
    filter_void_tuple_t<Ts...>>;

/** Helper to extract a single result, returning empty tuple for void.
    This is a separate function to work around a GCC-11 ICE that occurs
    when using nested immediately-invoked lambdas with pack expansion.
*/
template<std::size_t I, typename... Ts>
auto extract_single_result(when_all_state<Ts...>& state)
{
    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
    if constexpr (std::is_void_v<T>)
        return std::tuple<>();
    else
        return std::make_tuple(std::move(std::get<I>(state.results_)).get());
}

/** Extract results from state, filtering void types.
*/
template<typename... Ts>
auto extract_results(when_all_state<Ts...>& state)
{
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return std::tuple_cat(extract_single_result<Is>(state)...);
    }(std::index_sequence_for<Ts...>{});
}

} // namespace detail

/** Wait for all tasks to complete concurrently.

    @par Example
    @code
    task<void> example() {
        auto [a, b] = co_await when_all(
            fetch_int(),     // task<int>
            fetch_string()   // task<std::string>
        );
    }
    @endcode

    @param tasks The tasks to execute concurrently.
    @return A task yielding a tuple of results (void types filtered out).

    Key features:
    @li All child tasks are launched concurrently
    @li Results are collected in input order
    @li First error is captured; subsequent errors are discarded
    @li On error, stop is requested for all siblings
    @li Completes only after all children have completed
    @li Void tasks do not contribute to the result tuple
    @li Properly propagates frame allocators to all child coroutines
*/
template<typename... Ts>
[[nodiscard]] task<detail::when_all_result_t<Ts...>>
when_all(task<Ts>... tasks)
{
    using result_type = detail::when_all_result_t<Ts...>;

    // State is stored in the coroutine frame, using the frame allocator
    detail::when_all_state<Ts...> state;

    // Store tasks in the frame
    std::tuple<task<Ts>...> task_tuple(std::move(tasks)...);

    // Launch all tasks and wait for completion
    co_await detail::when_all_launcher<Ts...>(&task_tuple, &state);

    // Propagate first exception if any.
    // Safe without explicit acquire: capture_exception() is sequenced-before
    // signal_completion()'s acq_rel fetch_sub, which synchronizes-with the
    // last task's decrement that resumes this coroutine.
    if(state.first_exception_)
        std::rethrow_exception(state.first_exception_);

    // Extract and return results
    if constexpr (std::is_void_v<result_type>)
        co_return;
    else
        co_return detail::extract_results(state);
}

// For backwards compatibility and type queries, expose result type computation
template<typename... Ts>
using when_all_result_type = detail::when_all_result_t<Ts...>;

} // namespace capy
} // namespace boost

#endif
