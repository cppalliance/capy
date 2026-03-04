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
#include <coroutine>
#include <boost/capy/ex/io_env.hpp>
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
    std::array<std::coroutine_handle<>, task_count> runner_handles_{};

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
    std::coroutine_handle<> continuation_;
    io_env const* caller_env_ = nullptr;

    when_all_state()
        : remaining_count_(task_count)
    {
    }

    // Runners self-destruct in final_suspend. No destruction needed here.

    /** Capture an exception (first one wins).
    */
    void capture_exception(std::exception_ptr ep)
    {
        bool expected = false;
        if(has_exception_.compare_exchange_strong(
            expected, true, std::memory_order_relaxed))
            first_exception_ = ep;
    }

};

/** Wrapper coroutine that intercepts task completion.

    This runner awaits its assigned task and stores the result in
    the shared state, or captures the exception and requests stop.
*/
template<typename T, typename... Ts>
struct when_all_runner
{
    struct promise_type // : frame_allocating_base  // DISABLED FOR TESTING
    {
        when_all_state<Ts...>* state_ = nullptr;
        io_env env_;

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

                auto await_suspend(std::coroutine_handle<> h) noexcept
                {
                    // Extract everything needed before self-destruction.
                    auto* state = p_->state_;
                    auto* counter = &state->remaining_count_;
                    auto* caller_env = state->caller_env_;
                    auto cont = state->continuation_;

                    h.destroy();

                    // If last runner, dispatch parent for symmetric transfer.
                    auto remaining = counter->fetch_sub(1, std::memory_order_acq_rel);
                    if(remaining == 1)
                        return detail::symmetric_transfer(caller_env->executor.dispatch(cont));
                    return detail::symmetric_transfer(std::noop_coroutine());
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

            decltype(auto) await_resume()
            {
                return a_.await_resume();
            }

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

    explicit when_all_runner(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }

    // Enable move for all clang versions - some versions need it
    when_all_runner(when_all_runner&& other) noexcept : h_(std::exchange(other.h_, nullptr)) {}

    // Non-copyable
    when_all_runner(when_all_runner const&) = delete;
    when_all_runner& operator=(when_all_runner const&) = delete;
    when_all_runner& operator=(when_all_runner&&) = delete;

    auto release() noexcept
    {
        return std::exchange(h_, nullptr);
    }
};

/** Create a runner coroutine for a single awaitable.

    Awaitable is passed directly to ensure proper coroutine frame storage.
*/
template<std::size_t Index, IoAwaitable Awaitable, typename... Ts>
when_all_runner<awaitable_result_t<Awaitable>, Ts...>
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

/** Internal awaitable that launches all runner coroutines and waits.

    This awaitable is used inside the when_all coroutine to handle
    the concurrent execution of child awaitables.
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
        state_->continuation_ = continuation;
        state_->caller_env_ = caller_env;

        // Forward parent's stop requests to children
        if(caller_env->stop_token.stop_possible())
        {
            state_->parent_stop_callback_.emplace(
                caller_env->stop_token,
                typename state_type::stop_callback_fn{&state_->stop_source_});

            if(caller_env->stop_token.stop_requested())
                state_->stop_source_.request_stop();
        }

        // CRITICAL: If the last task finishes synchronously then the parent
        // coroutine resumes, destroying its frame, and destroying this object
        // prior to the completion of await_suspend. Therefore, await_suspend
        // must ensure `this` cannot be referenced after calling `launch_one`
        // for the last time.
        auto token = state_->stop_source_.get_token();
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (..., launch_one<Is>(caller_env->executor, token));
        }(std::index_sequence_for<Awaitables...>{});

        // Let signal_completion() handle resumption
        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
        // Results are extracted by the when_all coroutine from state
    }

private:
    template<std::size_t I>
    void launch_one(executor_ref caller_ex, std::stop_token token)
    {
        auto runner = make_when_all_runner<I>(
            std::move(std::get<I>(*awaitables_)), state_);

        auto h = runner.release();
        h.promise().state_ = state_;
        h.promise().env_ = io_env{caller_ex, token, state_->caller_env_->frame_allocator};

        std::coroutine_handle<> ch{h};
        state_->runner_handles_[I] = ch;
        state_->caller_env_->executor.post(ch);
    }
};

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

/** Compute a tuple type with void types filtered out.

    Returns void when all types are void (P2300 aligned),
    otherwise returns a std::tuple with void types removed.

    Example: non_void_tuple_t<int, void, string> = std::tuple<int, string>
    Example: non_void_tuple_t<void, void> = void
*/
template<typename... Ts>
using non_void_tuple_t = std::conditional_t<
    std::is_same_v<detail::filter_void_tuple_t<Ts...>, std::tuple<>>,
    void,
    detail::filter_void_tuple_t<Ts...>>;

/** Execute multiple awaitables concurrently and collect their results.

    Launches all awaitables simultaneously and waits for all to complete
    before returning. Results are collected in input order. If any
    awaitable throws, cancellation is requested for siblings and the first
    exception is rethrown after all awaitables complete.

    @li All child awaitables run concurrently on the caller's executor
    @li Results are returned as a tuple in input order
    @li Void-returning awaitables do not contribute to the result tuple
    @li If all awaitables return void, `when_all` returns `task<void>`
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

    @return A task yielding a tuple of non-void results. Returns
        `task<void>` when all input awaitables return void.

    @par Example

    @code
    task<> example()
    {
        // Concurrent fetch, results collected in order
        auto [user, posts] = co_await when_all(
            fetch_user( id ),      // task<User>
            fetch_posts( id )      // task<std::vector<Post>>
        );

        // Void awaitables don't contribute to result
        co_await when_all(
            log_event( "start" ),  // task<void>
            notify_user( id )      // task<void>
        );
        // Returns task<void>, no result tuple
    }
    @endcode

    @see IoAwaitable, task
*/
template<IoAwaitable... As>
[[nodiscard]] auto when_all(As... awaitables)
    -> task<non_void_tuple_t<awaitable_result_t<As>...>>
{
    using result_type = non_void_tuple_t<awaitable_result_t<As>...>;

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
    if(state.first_exception_)
        std::rethrow_exception(state.first_exception_);

    // Extract and return results
    if constexpr (std::is_void_v<result_type>)
        co_return;
    else
        co_return detail::extract_results(state);
}

} // namespace capy
} // namespace boost

#endif
