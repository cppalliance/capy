//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXAMPLE_AWAITABLE_SENDER_HPP
#define BOOST_CAPY_EXAMPLE_AWAITABLE_SENDER_HPP

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>

#include <beman/execution/execution.hpp>

#include <concepts>
#include <coroutine>
#include <exception>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>

namespace boost::capy {

// -------------------------------------------------------
// CPO: query a receiver environment for a Capy executor
// -------------------------------------------------------

struct get_io_executor_t
{
    template<class Env>
    auto operator()(Env const& env) const noexcept
        -> decltype(env.query(std::declval<get_io_executor_t const&>()))
    {
        return env.query(*this);
    }
};

inline constexpr get_io_executor_t get_io_executor{};

// -------------------------------------------------------
// Environment that carries a Capy executor + stop token
// -------------------------------------------------------

struct io_sender_env
{
    executor_ref io_executor;
    std::stop_token stop_token;

    auto query(
        get_io_executor_t const&) const noexcept
        -> executor_ref
    {
        return io_executor;
    }

    auto query(
        beman::execution::get_stop_token_t const&) const noexcept
        -> std::stop_token
    {
        return stop_token;
    }
};

namespace detail {

template<class T, class = void>
struct has_tuple_protocol : std::false_type {};

template<class T>
struct has_tuple_protocol<T,
    std::void_t<
        typename std::tuple_size<T>::type,
        typename std::tuple_element<0, T>::type>>
    : std::true_type {};

template<class T, bool = has_tuple_protocol<T>::value>
struct is_ec_outcome : std::is_same<T, std::error_code> {};

template<class T>
struct is_ec_outcome<T, true>
    : std::bool_constant<
        std::tuple_size_v<T> == 1 &&
        std::is_same_v<
            std::tuple_element_t<0, T>,
            std::error_code>>
{};

template<class T>
constexpr bool is_ec_outcome_v =
    std::is_same_v<T, std::error_code> ||
    is_ec_outcome<T>::value;

template<class T, bool = has_tuple_protocol<T>::value>
struct is_compound_ec_result : std::false_type {};

template<class T>
struct is_compound_ec_result<T, true>
    : std::bool_constant<
        std::tuple_size_v<T> >= 2 &&
        std::is_same_v<
            std::tuple_element_t<0, T>,
            std::error_code>>
{};

template<class T>
constexpr bool is_compound_ec_result_v =
    is_compound_ec_result<T>::value;

// -------------------------------------------------------
// Bridge coroutine: provides a coroutine_handle to feed
// into IoAwaitable::await_suspend, then relays the result
// to the sender receiver.
// -------------------------------------------------------

template<class IoAw, class Receiver>
struct bridge_task
{
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type
    {
        io_env const* env_ = nullptr;

        bridge_task get_return_object() noexcept
        {
            return bridge_task{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}

        // Wraps the IoAwaitable so its await_suspend
        // receives the io_env* from this promise.
        template<class A>
        struct transform_awaiter
        {
            std::decay_t<A>& aw_;
            promise_type* p_;

            bool await_ready() noexcept
            {
                return aw_.await_ready();
            }

            decltype(auto) await_resume()
            {
                return aw_.await_resume();
            }

            auto await_suspend(std::coroutine_handle<> h) noexcept
            {
                return aw_.await_suspend(h, p_->env_);
            }
        };

        template<class A>
        auto await_transform(A&& a)
        {
            return transform_awaiter<A>{a, this};
        }
    };

    handle_type h_{};

    ~bridge_task()
    {
        if(h_)
            h_.destroy();
    }

    bridge_task() noexcept = default;

    bridge_task(bridge_task&& o) noexcept
        : h_(std::exchange(o.h_, {}))
    {
    }

    bridge_task& operator=(bridge_task&& o) noexcept
    {
        if(h_)
            h_.destroy();
        h_ = std::exchange(o.h_, {});
        return *this;
    }

    bridge_task(bridge_task const&) = delete;
    bridge_task& operator=(bridge_task const&) = delete;

private:
    explicit bridge_task(handle_type h) noexcept
        : h_(h)
    {
    }
};

} // namespace detail

// -------------------------------------------------------
// Sender that wraps an IoAwaitable
// -------------------------------------------------------

template<class IoAw>
struct awaitable_sender
{
    using sender_concept = beman::execution::sender_t;

    using result_type = decltype(
        std::declval<std::decay_t<IoAw>&>().await_resume());

    static auto make_sigs()
    {
        if constexpr (std::is_void_v<result_type>)
            return beman::execution::completion_signatures<
                beman::execution::set_value_t(),
                beman::execution::set_error_t(std::exception_ptr),
                beman::execution::set_stopped_t()>{};
        else if constexpr (
            detail::is_ec_outcome_v<result_type>)
            return beman::execution::completion_signatures<
                beman::execution::set_value_t(),
                beman::execution::set_error_t(std::error_code),
                beman::execution::set_error_t(std::exception_ptr),
                beman::execution::set_stopped_t()>{};
        else
            return beman::execution::completion_signatures<
                beman::execution::set_value_t(result_type),
                beman::execution::set_error_t(std::exception_ptr),
                beman::execution::set_stopped_t()>{};
    }

    using completion_signatures = decltype(make_sigs());

    IoAw aw_;

    template<class Receiver>
    struct op_state
    {
        using operation_state_concept =
            beman::execution::operation_state_t;

        IoAw aw_;
        Receiver rcvr_;
        io_env env_;
        detail::bridge_task<IoAw, Receiver> bridge_;

        op_state(IoAw aw, Receiver rcvr)
            : aw_(std::move(aw))
            , rcvr_(std::move(rcvr))
        {
        }

        op_state(op_state const&) = delete;
        op_state(op_state&&) = delete;
        op_state& operator=(op_state const&) = delete;
        op_state& operator=(op_state&&) = delete;

        void start() noexcept
        {
            auto renv = beman::execution::get_env(rcvr_);
            auto ex = get_io_executor(renv);

            std::stop_token st;
            if constexpr (requires {
                { renv.query(beman::execution::get_stop_token_t{}) }
                    -> std::convertible_to<std::stop_token>; })
            {
                st = renv.query(
                    beman::execution::get_stop_token_t{});
            }

            env_ = io_env{ex, st, nullptr};

            bridge_ = [](IoAw aw, Receiver rcvr)
                -> detail::bridge_task<IoAw, Receiver>
            {
                try
                {
                    if constexpr (std::is_void_v<result_type>)
                    {
                        co_await std::move(aw);
                        beman::execution::set_value(
                            std::move(rcvr));
                    }
                    else if constexpr (
                        detail::is_ec_outcome_v<result_type>)
                    {
                        auto result = co_await std::move(aw);
                        std::error_code ec;
                        if constexpr (std::is_same_v<
                            result_type, std::error_code>)
                            ec = result;
                        else
                            ec = get<0>(result);
                        if (!ec)
                            beman::execution::set_value(
                                std::move(rcvr));
                        else
                            beman::execution::set_error(
                                std::move(rcvr), ec);
                    }
                    else
                    {
                        auto result = co_await std::move(aw);
                        beman::execution::set_value(
                            std::move(rcvr),
                            std::move(result));
                    }
                }
                catch(...)
                {
                    beman::execution::set_error(
                        std::move(rcvr),
                        std::current_exception());
                }
            }(std::move(aw_), std::move(rcvr_));

            bridge_.h_.promise().env_ = &env_;
            bridge_.h_.resume();
        }
    };

    template<class Receiver>
    auto connect(Receiver rcvr) &&
        -> op_state<Receiver>
    {
        return op_state<Receiver>(
            std::move(aw_), std::move(rcvr));
    }

    template<class Receiver>
    auto connect(Receiver rcvr) const&
        -> op_state<Receiver>
    {
        return op_state<Receiver>(aw_, std::move(rcvr));
    }
};

/** Create a beman::execution sender from an IoAwaitable.

    The bridge routes the awaitable's result through sender
    channels based on its type:

    - `void` - calls `set_value()`.
    - `error_code` (or a single-element tuple-like whose
      element 0 is `error_code`) - calls `set_value()`
      when the code is zero, `set_error(ec)` otherwise.
    - Any other single value `T` - calls `set_value(T)`.
    - Compound results whose element 0 is `error_code`
      with additional elements are rejected at compile
      time. Wrap the operation in a `task<error_code>`
      that inspects the compound result and returns the
      error code.

    @par Example
    @code
    auto sndr = as_sender(capy::delay(
        std::chrono::milliseconds(100)));
    @endcode

    @param aw The IoAwaitable to wrap.
    @return A sender whose completion channels reflect
        the awaitable's result type.
*/
template<class IoAw>
auto as_sender(IoAw&& aw)
{
    using R = decltype(
        std::declval<std::decay_t<IoAw>&>().await_resume());
    static_assert(
        !detail::is_compound_ec_result_v<std::decay_t<R>>,
        "as_sender does not accept awaitables whose result "
        "destructures into (error_code, ...). Wrap the "
        "operation in a task<error_code> that inspects the "
        "compound result and returns the error code.");
    return awaitable_sender<std::decay_t<IoAw>>{
        std::forward<IoAw>(aw)};
}

} // namespace boost::capy

#endif
