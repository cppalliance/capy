//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXAMPLE_AWAITABLE_SENDER_HPP
#define BOOST_CAPY_EXAMPLE_AWAITABLE_SENDER_HPP

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
// frame_cb: synthetic coroutine frame for callback handles
//
// The first two members match the coroutine frame layout
// used by MSVC, GCC, and Clang. from_address produces a
// coroutine_handle whose .resume() calls our function
// pointer and whose .destroy() is a no-op.
// -------------------------------------------------------

struct frame_cb
{
    void (*resume)(frame_cb*);
    void (*destroy)(frame_cb*);
    void* data;
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
        detail::frame_cb cb_;

        op_state(IoAw aw, Receiver rcvr)
            : aw_(std::move(aw))
            , rcvr_(std::move(rcvr))
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

            // Not a real coroutine caller, so symmetric transfer
            // must be driven by hand: any non-noop handle (our own
            // frame on immediate completion, or a wrapped task's
            // handle that still needs to run) has to be resumed
            // explicitly or nothing ever completes.
            auto resumed = detail::call_await_suspend(&aw_, h, &env_);
            if(resumed != std::noop_coroutine())
                resumed.resume();
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
    auto sndr = as_sender(waker.wait());
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

    @par Example
    @code
    do_read(sock, buf)
        | split_ec()
        | ex::upon_error(
            [](std::error_code ec) {
                // reachable, no exceptions
            });
    @endcode

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
