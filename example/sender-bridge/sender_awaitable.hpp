//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXAMPLE_SENDER_AWAITABLE_HPP
#define BOOST_CAPY_EXAMPLE_SENDER_AWAITABLE_HPP

#include <boost/capy/ex/io_env.hpp>

#include <beman/execution/execution.hpp>

#include <coroutine>
#include <cstring>
#include <exception>
#include <new>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <variant>

namespace boost::capy {

namespace detail {

struct stopped_t {};

// The receiver's environment exposes only the stop token.
struct bridge_env
{
    std::stop_token st_;

    auto query(
        beman::execution::get_stop_token_t const&) const noexcept
    {
        return st_;
    }
};

// Deduce the single value tuple type from a sender's completion
// signatures using beman::execution::value_types_of_t.
template<class Sender>
using sender_single_value_t =
    beman::execution::value_types_of_t<
        Sender,
        bridge_env,
        std::tuple,
        std::type_identity_t>;

// Bridge receiver that stores the sender's completion result
// and posts the coroutine handle back through the Capy executor.
template<class ValueTuple>
struct bridge_receiver
{
    using receiver_concept = beman::execution::receiver_t;

    std::variant<
        std::monostate,
        ValueTuple,
        std::exception_ptr,
        stopped_t>*         result_;
    std::coroutine_handle<> cont_;
    io_env const*           env_;

    auto get_env() const noexcept -> bridge_env
    {
        return {env_->stop_token};
    }

    template<class... Args>
    void set_value(Args&&... args) && noexcept
    {
        result_->template emplace<1>(
            std::forward<Args>(args)...);
        env_->executor.post(cont_);
    }

    template<class E>
    void set_error(E&& e) && noexcept
    {
        if constexpr (
            std::is_same_v<
                std::decay_t<E>, std::exception_ptr>)
            result_->template emplace<2>(
                std::forward<E>(e));
        else
            result_->template emplace<2>(
                std::make_exception_ptr(
                    std::forward<E>(e)));
        env_->executor.post(cont_);
    }

    void set_stopped() && noexcept
    {
        result_->template emplace<3>(stopped_t{});
        env_->executor.post(cont_);
    }
};

} // namespace detail

/** Awaitable that bridges a beman::execution sender into a Capy coroutine.

    Satisfies IoAwaitable. When co_awaited inside a capy::task,
    connects the sender to a bridge receiver, starts the operation,
    and resumes the coroutine on the caller's executor when the
    sender completes.

    Stop token propagation: the Capy coroutine's stop_token is
    forwarded to the sender through the bridge receiver's
    environment.

    @tparam Sender The beman::execution sender type.
*/
template<class Sender>
struct [[nodiscard]] sender_awaitable
{
    using value_tuple = detail::sender_single_value_t<Sender>;
    using receiver_type = detail::bridge_receiver<value_tuple>;
    using op_state_type = decltype(
        beman::execution::connect(
            std::declval<Sender>(),
            std::declval<receiver_type>()));

    Sender sndr_;

    std::variant<
        std::monostate,
        value_tuple,
        std::exception_ptr,
        detail::stopped_t> result_{};

    alignas(op_state_type)
    unsigned char op_buf_[sizeof(op_state_type)];
    bool op_constructed_ = false;

    explicit sender_awaitable(Sender sndr)
        : sndr_(std::move(sndr))
    {
    }

    // Movable only before await_suspend (op_state not yet constructed)
    sender_awaitable(sender_awaitable&& o) noexcept(
        std::is_nothrow_move_constructible_v<Sender>)
        : sndr_(std::move(o.sndr_))
    {
    }

    sender_awaitable(sender_awaitable const&) = delete;
    sender_awaitable& operator=(sender_awaitable const&) = delete;
    sender_awaitable& operator=(sender_awaitable&&) = delete;

    ~sender_awaitable()
    {
        if(op_constructed_)
            std::launder(
                reinterpret_cast<op_state_type*>(
                    op_buf_))->~op_state_type();
    }

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<>
    await_suspend(
        std::coroutine_handle<> h,
        io_env const* env)
    {
        ::new(op_buf_) op_state_type(
            beman::execution::connect(
                std::move(sndr_),
                receiver_type{&result_, h, env}));
        op_constructed_ = true;
        beman::execution::start(
            *std::launder(
                reinterpret_cast<op_state_type*>(
                    op_buf_)));
        return std::noop_coroutine();
    }

    auto await_resume()
    {
        if(result_.index() == 2)
            std::rethrow_exception(
                std::get<2>(result_));
        if(result_.index() == 3)
            throw std::runtime_error(
                "sender completed with set_stopped");

        if constexpr (std::tuple_size_v<value_tuple> == 0)
            return;
        else if constexpr (std::tuple_size_v<value_tuple> == 1)
            return std::get<0>(
                std::get<1>(std::move(result_)));
        else
            return std::get<1>(std::move(result_));
    }
};

/** Create an IoAwaitable from a beman::execution sender.

    @par Example
    @code
    capy::task<int> compute(auto sched)
    {
        auto result = co_await await_sender(
            beman::execution::schedule(sched)
                | beman::execution::then(
                    [] { return 42; }));
        co_return result;
    }
    @endcode

    @param sndr The sender to bridge.
    @return An IoAwaitable that can be co_awaited in a capy::task.
*/
template<class Sender>
auto await_sender(Sender&& sndr)
{
    return sender_awaitable<std::decay_t<Sender>>(
        std::forward<Sender>(sndr));
}

} // namespace boost::capy

#endif
