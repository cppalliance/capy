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
#include <boost/capy/concept/affine_awaitable.hpp>
#include <boost/capy/frame_allocator.hpp>
#include <boost/capy/task.hpp>

#include <array>
#include <atomic>
#include <exception>
#include <memory>
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
template<typename... Ts>
struct filter_void_tuple;

template<>
struct filter_void_tuple<>
{
    using type = std::tuple<>;
};

template<typename T, typename... Rest>
struct filter_void_tuple<T, Rest...>
{
private:
    using rest_type = typename filter_void_tuple<Rest...>::type;

public:
    using type = std::conditional_t<
        std::is_void_v<T>,
        rest_type,
        decltype(std::tuple_cat(
            std::declval<std::tuple<T>>(),
            std::declval<rest_type>()))>;
};

template<typename... Ts>
using filter_void_tuple_t = typename filter_void_tuple<Ts...>::type;

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
    void set()
    {
    }
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
    std::array<coro, task_count> runner_handles_{};

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
    coro continuation_;
    any_dispatcher caller_dispatcher_;

    explicit when_all_state(std::size_t count)
        : remaining_count_(count)
    {
    }

    ~when_all_state()
    {
        destroy_runners();
    }

    void store_runner(std::size_t index, coro h)
    {
        runner_handles_[index] = h;
    }

    void destroy_runners()
    {
        for(auto& h : runner_handles_)
        {
            if(h)
            {
                h.destroy();
                h = nullptr;
            }
        }
    }

    /** Capture an exception (first one wins).
    */
    bool capture_exception(std::exception_ptr ep)
    {
        bool expected = false;
        if(has_exception_.compare_exchange_strong(
            expected, true, std::memory_order_relaxed))
        {
            first_exception_ = ep;
            return true;
        }
        return false;
    }

    /** Signal that a task has completed.

        The last child to complete triggers resumption of the parent.
    */
    coro signal_completion()
    {
        auto remaining = remaining_count_.fetch_sub(1, std::memory_order_acq_rel);
        if(remaining == 1)
            return caller_dispatcher_(continuation_);
        return std::noop_coroutine();
    }

};

/** Wrapper coroutine that intercepts task completion.

    This runner awaits its assigned task and stores the result in
    the shared state, or captures the exception and requests stop.
*/
template<typename T, std::size_t Index, typename... Ts>
struct when_all_runner
{
    struct promise_type : frame_allocating_base
    {
        when_all_state<Ts...>* state_ = nullptr;
        any_dispatcher ex_;
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

                coro await_suspend(coro) noexcept
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
                using A = std::decay_t<Awaitable>;
                // Propagate stop_token to nested awaitables
                if constexpr (stoppable_awaitable<A, any_dispatcher>)
                    return a_.await_suspend(h, p_->ex_, p_->stop_token_);
                else
                    return a_.await_suspend(h, p_->ex_);
            }
        };

        template<class Awaitable>
        auto await_transform(Awaitable&& a)
        {
            using A = std::decay_t<Awaitable>;
            if constexpr (affine_awaitable<A, any_dispatcher>)
            {
                return transform_awaiter<Awaitable>{
                    std::forward<Awaitable>(a), this};
            }
            else
            {
                return make_affine(std::forward<Awaitable>(a), ex_);
            }
        }
    };

    std::coroutine_handle<promise_type> h_;

    explicit when_all_runner(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }

    ~when_all_runner()
    {
        if(h_)
            h_.destroy();
    }

    when_all_runner(when_all_runner const&) = delete;
    when_all_runner& operator=(when_all_runner const&) = delete;

    when_all_runner(when_all_runner&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    when_all_runner& operator=(when_all_runner&& other) noexcept
    {
        if(this != &other)
        {
            if(h_)
                h_.destroy();
            h_ = std::exchange(other.h_, nullptr);
        }
        return *this;
    }

    auto release() noexcept
    {
        return std::exchange(h_, nullptr);
    }
};

/** Create a runner coroutine for a single task.
*/
template<typename T, std::size_t Index, typename... Ts>
when_all_runner<T, Index, Ts...>
make_when_all_runner(task<T> inner, when_all_state<Ts...>* state)
{
    if constexpr (std::is_void_v<T>)
    {
        co_await std::move(inner);
        std::get<Index>(state->results_).set();
    }
    else
    {
        auto result = co_await std::move(inner);
        std::get<Index>(state->results_).set(std::move(result));
    }
}

} // namespace detail

/** Awaitable that concurrently executes multiple tasks.

    @tparam Ts The return types of the tasks.

    Key features:
    @li All child tasks are launched concurrently
    @li Results are collected in input order
    @li First error is captured; subsequent errors are discarded
    @li On error, stop is requested for all siblings
    @li Completes only after all children have completed
    @li Void tasks do not contribute to the result tuple
*/
template<typename... Ts>
class when_all_awaitable
{
    using state_type = detail::when_all_state<Ts...>;
    using filtered_tuple = detail::filter_void_tuple_t<Ts...>;

public:
    /** Result type with void tasks filtered out.
        Returns void when all tasks are void (P2300 aligned).
    */
    using result_type = std::conditional_t<
        std::is_same_v<filtered_tuple, std::tuple<>>,
        void,
        filtered_tuple>;

private:
    std::tuple<task<Ts>...> tasks_;
    std::unique_ptr<state_type> state_;

public:
    explicit when_all_awaitable(task<Ts>... tasks)
        : tasks_(std::move(tasks)...)
    {
    }

    when_all_awaitable(when_all_awaitable const&) = delete;
    when_all_awaitable& operator=(when_all_awaitable const&) = delete;
    when_all_awaitable(when_all_awaitable&&) = default;
    when_all_awaitable& operator=(when_all_awaitable&&) = default;

    bool await_ready() const noexcept
    {
        return sizeof...(Ts) == 0;
    }

    /** Affine awaitable protocol.
    */
    template<dispatcher D>
    coro await_suspend(coro continuation, D const& caller_ex)
    {
        return await_suspend_impl(continuation, caller_ex, std::stop_token{});
    }

    /** Stoppable awaitable protocol.
    */
    template<dispatcher D>
    coro await_suspend(coro continuation, D const& caller_ex, std::stop_token token)
    {
        return await_suspend_impl(continuation, caller_ex, token);
    }

    /** Extract results or propagate the first captured error.
    */
    result_type await_resume()
    {
        if(state_->first_exception_)
            std::rethrow_exception(state_->first_exception_);

        if constexpr (std::is_void_v<result_type>)
            return;
        else
            return extract_results_impl<0>();
    }

private:
    template<dispatcher D>
    coro await_suspend_impl(coro continuation, D const& caller_ex, std::stop_token parent_token)
    {
        state_ = std::make_unique<state_type>(sizeof...(Ts));
        state_->continuation_ = continuation;
        state_->caller_dispatcher_ = caller_ex;

        // Forward parent's stop requests to children
        if(parent_token.stop_possible())
        {
            state_->parent_stop_callback_.emplace(
                parent_token,
                typename state_type::stop_callback_fn{&state_->stop_source_});

            if(parent_token.stop_requested())
                state_->stop_source_.request_stop();
        }

        // Launch all tasks concurrently
        launch_all(caller_ex, std::index_sequence_for<Ts...>{});

        // Let signal_completion() handle resumption to avoid double-resume
        return std::noop_coroutine();
    }

    template<dispatcher D, std::size_t... Is>
    void launch_all(D const& caller_ex, std::index_sequence<Is...>)
    {
        (..., launch_one<Is>(caller_ex));
    }

    template<std::size_t I, dispatcher D>
    void launch_one(D const& caller_ex)
    {
        using T = std::tuple_element_t<I, std::tuple<Ts...>>;

        auto runner = detail::make_when_all_runner<T, I, Ts...>(
            std::move(std::get<I>(tasks_)), state_.get());

        auto h = runner.release();
        h.promise().state_ = state_.get();
        h.promise().ex_ = caller_ex;

        // Give child a stop_token connected to our stop_source
        h.promise().stop_token_ = state_->stop_source_.get_token();

        state_->store_runner(I, coro{h});

        // Start the task via dispatcher
        caller_ex(coro{h}).resume();
    }

    // Recursively build result tuple, skipping void types
    template<std::size_t I>
    auto extract_results_impl()
    {
        if constexpr (I >= sizeof...(Ts))
            return std::tuple<>();
        else
        {
            using T = std::tuple_element_t<I, std::tuple<Ts...>>;
            if constexpr (std::is_void_v<T>)
                return extract_results_impl<I + 1>();
            else
                return std::tuple_cat(
                    std::make_tuple(std::move(std::get<I>(state_->results_)).get()),
                    extract_results_impl<I + 1>());
        }
    }
};

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
    @return An awaitable yielding a tuple of results.
*/
template<typename... Ts>
[[nodiscard]] auto when_all(task<Ts>... tasks)
{
    return when_all_awaitable<Ts...>(std::move(tasks)...);
}

} // namespace capy
} // namespace boost

#endif
