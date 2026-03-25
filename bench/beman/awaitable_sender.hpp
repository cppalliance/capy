//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_AWAITABLE_SENDER_HPP
#define BOOST_CAPY_BENCH_AWAITABLE_SENDER_HPP

#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
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

// Query CPO for obtaining a Capy-compatible executor
// from a P2300 environment. The returned object must
// satisfy Capy's Executor concept. Environments that
// host IoAwaitables via the as_sender bridge must
// answer this query.
struct get_io_executor_t
{
    constexpr bool query(
        beman::execution::forwarding_query_t const&)
            const noexcept
    {
        return true;
    }

    template<class Env>
        requires requires(Env const& env) {
            env.query(
                std::declval<get_io_executor_t const&>());
        }
    auto operator()(Env const& env) const noexcept
    {
        return env.query(*this);
    }
};

inline constexpr get_io_executor_t get_io_executor{};

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

struct frame_cb
{
    void (*resume)(frame_cb*);
    void (*destroy)(frame_cb*);
    void* data;
};

} // namespace detail

/** Sender that wraps an IoAwaitable.

    When connected or co_awaited, the bridge queries
    the receiver's or promise's environment for a
    Capy-compatible executor via get_io_executor.
    The executor is stored by value in the operation
    state and used to construct the io_env passed to
    the IoAwaitable's await_suspend.

    @tparam IoAw The IoAwaitable type.
*/
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
            detail::is_compound_ec_result_v<result_type>)
            return beman::execution::completion_signatures<
                beman::execution::set_value_t(
                    std::tuple_element_t<1, result_type>),
                beman::execution::set_error_t(std::error_code),
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

        using executor_type = decltype(
            beman::execution::get_scheduler(
                beman::execution::get_env(
                    std::declval<Receiver const&>()))
                        .query(get_io_executor_t{}));

        IoAw aw_;
        Receiver rcvr_;
        executor_type ex_;
        io_env env_;
        detail::frame_cb cb_;

        op_state(IoAw aw, Receiver rcvr)
            : aw_(std::move(aw))
            , rcvr_(std::move(rcvr))
            , ex_{}
            , cb_{}
        {
        }

        op_state(op_state const&) = delete;
        op_state(op_state&&) = delete;
        op_state& operator=(op_state const&) = delete;
        op_state& operator=(op_state&&) = delete;

        static void
        on_resume(detail::frame_cb* p) noexcept
        {
            auto* self = static_cast<op_state*>(p->data);
            self->complete();
        }

        static void
        on_destroy(detail::frame_cb*) noexcept
        {
        }

        void complete() noexcept
        {
            try
            {
                if constexpr (std::is_void_v<result_type>)
                {
                    aw_.await_resume();
                    if(env_.stop_token.stop_requested())
                        beman::execution::set_stopped(
                            std::move(rcvr_));
                    else
                        beman::execution::set_value(
                            std::move(rcvr_));
                }
                else if constexpr (
                    detail::is_compound_ec_result_v<result_type>)
                {
                    auto result = aw_.await_resume();
                    if(env_.stop_token.stop_requested())
                    {
                        beman::execution::set_stopped(
                            std::move(rcvr_));
                    }
                    else
                    {
                        auto ec = get<0>(result);
                        if(!ec)
                            beman::execution::set_value(
                                std::move(rcvr_),
                                get<1>(std::move(result)));
                        else
                            beman::execution::set_error(
                                std::move(rcvr_), ec);
                    }
                }
                else if constexpr (
                    detail::is_ec_outcome_v<result_type>)
                {
                    auto result = aw_.await_resume();
                    if(env_.stop_token.stop_requested())
                    {
                        beman::execution::set_stopped(
                            std::move(rcvr_));
                    }
                    else
                    {
                        std::error_code ec;
                        if constexpr (std::is_same_v<
                            result_type, std::error_code>)
                            ec = result;
                        else
                            ec = get<0>(result);
                        if(!ec)
                            beman::execution::set_value(
                                std::move(rcvr_));
                        else
                            beman::execution::set_error(
                                std::move(rcvr_), ec);
                    }
                }
                else
                {
                    auto result = aw_.await_resume();
                    if(env_.stop_token.stop_requested())
                        beman::execution::set_stopped(
                            std::move(rcvr_));
                    else
                        beman::execution::set_value(
                            std::move(rcvr_),
                            std::move(result));
                }
            }
            catch(...)
            {
                beman::execution::set_error(
                    std::move(rcvr_),
                    std::current_exception());
            }
        }

        void start() noexcept
        {
            auto renv = beman::execution::get_env(rcvr_);
            ex_ = beman::execution::get_scheduler(renv)
                .query(get_io_executor_t{});

            std::stop_token st;
            if constexpr (requires {
                { renv.query(beman::execution::get_stop_token_t{}) }
                    -> std::convertible_to<std::stop_token>; })
            {
                st = renv.query(
                    beman::execution::get_stop_token_t{});
            }

            env_ = io_env{ex_, st, nullptr};

            if(aw_.await_ready())
            {
                complete();
                return;
            }

            cb_.resume = &on_resume;
            cb_.destroy = &on_destroy;
            cb_.data = this;

            auto h = std::coroutine_handle<>::from_address(
                static_cast<void*>(&cb_));

            detail::call_await_suspend(&aw_, h, &env_);
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

    // Bypass beman's sender_awaitable when co_awaited
    // from a bex::task. Adapts the IoAwaitable's 2-arg
    // await_suspend to standard 1-arg protocol, avoiding
    // the double bridge (as_sender + sender_awaitable).
    template<class Promise>
    auto as_awaitable(Promise& promise) &&
    {
        auto penv = promise.get_env();
        auto sched = beman::execution::get_scheduler(penv);

        using executor_type = decltype(
            sched.query(get_io_executor_t{}));

        auto ex = sched.query(get_io_executor_t{});

        std::stop_token st;
        if constexpr (requires {
            { penv.query(beman::execution::get_stop_token_t{}) }
                -> std::convertible_to<std::stop_token>; })
        {
            st = penv.query(
                beman::execution::get_stop_token_t{});
        }

        struct aw
        {
            IoAw aw_;
            executor_type ex_;
            std::stop_token st_;
            io_env env_;

            bool await_ready() noexcept
            {
                return aw_.await_ready();
            }

            std::coroutine_handle<>
            await_suspend(std::coroutine_handle<> h)
            {
                env_ = io_env{ex_, st_, nullptr};
                return aw_.await_suspend(h, &env_);
            }

            auto await_resume()
            {
                return aw_.await_resume();
            }
        };

        return aw{std::move(aw_), std::move(ex), st, {}};
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

    When connected or co_awaited, the bridge queries the
    receiver's or promise's environment for a Capy executor
    via get_io_executor. The environment must answer this
    query with an object satisfying Capy's Executor concept.

    @param aw The IoAwaitable to wrap.
    @return A sender whose completion channels reflect
        the awaitable's result type.
*/
template<class IoAw>
auto as_sender(IoAw&& aw)
{
    return awaitable_sender<std::decay_t<IoAw>>{
        std::forward<IoAw>(aw)};
}

// -------------------------------------------------------
// split_ec: sender adapter that routes error_code to
// set_value() or set_error(ec) at runtime.
// -------------------------------------------------------

namespace detail {

template<class Sender>
struct split_ec_sender
{
    using sender_concept = beman::execution::sender_t;

    using completion_signatures =
        beman::execution::completion_signatures<
            beman::execution::set_value_t(),
            beman::execution::set_error_t(std::error_code),
            beman::execution::set_error_t(std::exception_ptr),
            beman::execution::set_stopped_t()>;

    Sender sndr_;

    template<class Receiver>
    struct ec_receiver
    {
        using receiver_concept = beman::execution::receiver_t;

        Receiver rcvr_;

        auto get_env() const noexcept
        {
            return beman::execution::get_env(rcvr_);
        }

        void set_value(std::error_code ec) && noexcept
        {
            if (!ec)
                beman::execution::set_value(
                    std::move(rcvr_));
            else
                beman::execution::set_error(
                    std::move(rcvr_), ec);
        }

        void set_value() && noexcept
        {
            beman::execution::set_value(
                std::move(rcvr_));
        }

        template<class E>
        void set_error(E&& e) && noexcept
        {
            beman::execution::set_error(
                std::move(rcvr_),
                std::forward<E>(e));
        }

        void set_stopped() && noexcept
        {
            beman::execution::set_stopped(
                std::move(rcvr_));
        }
    };

    template<class Receiver>
    struct op_state
    {
        using operation_state_concept =
            beman::execution::operation_state_t;

        using inner_op_t = decltype(
            beman::execution::connect(
                std::declval<Sender>(),
                std::declval<ec_receiver<Receiver>>()));

        inner_op_t op_;

        op_state(Sender sndr, Receiver rcvr)
            : op_(beman::execution::connect(
                std::move(sndr),
                ec_receiver<Receiver>{std::move(rcvr)}))
        {
        }

        op_state(op_state const&) = delete;
        op_state(op_state&&) = delete;
        op_state& operator=(op_state const&) = delete;
        op_state& operator=(op_state&&) = delete;

        void start() noexcept
        {
            beman::execution::start(op_);
        }
    };

    template<class Receiver>
    auto connect(Receiver rcvr) &&
        -> op_state<Receiver>
    {
        return op_state<Receiver>(
            std::move(sndr_), std::move(rcvr));
    }

    template<class Receiver>
    auto connect(Receiver rcvr) const&
        -> op_state<Receiver>
    {
        return op_state<Receiver>(
            sndr_, std::move(rcvr));
    }
};

} // namespace detail

/** Split an `error_code` value channel into success and error channels.

    Takes a sender that completes with `set_value(error_code)` and
    routes it at runtime: `set_value()` when the code is zero,
    `set_error(ec)` otherwise. No exceptions.

    @param sndr The predecessor sender.
    @return A sender completing with `set_value()`,
        `set_error(error_code)`, or `set_stopped()`.
*/
template<class Sender>
auto split_ec(Sender&& sndr)
{
    return detail::split_ec_sender<
        std::decay_t<Sender>>{
            std::forward<Sender>(sndr)};
}

} // namespace boost::capy

#endif
