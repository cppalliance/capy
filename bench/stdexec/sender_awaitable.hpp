//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_STDEXEC_SENDER_AWAITABLE_HPP
#define BOOST_CAPY_BENCH_STDEXEC_SENDER_AWAITABLE_HPP

#include <boost/capy/error.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>

#include <stdexec/execution.hpp>

#include <atomic>
#include <coroutine>
#include <exception>
#include <new>
#include <stop_token>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <variant>

namespace boost::capy {

namespace detail {

struct stopped_t {};

struct operation_cancelled {};

struct bridge_env
{
    std::stop_token st_;

    auto query(
        stdexec::get_stop_token_t const&)
            const noexcept
    {
        return st_;
    }
};

template<class Sender>
using sender_single_value_t =
    stdexec::value_types_of_t<
        Sender,
        bridge_env,
        std::tuple,
        std::type_identity_t>;

// Detect whether a sender can complete with
// set_error(std::error_code).
template<class Sender>
struct has_error_code_completion
{
    template<class... Es>
    struct checker
    {
        static constexpr bool value =
            (std::is_same_v<
                Es, std::error_code> || ...);
    };

    static constexpr bool value =
        stdexec::error_types_of_t<
            Sender,
            bridge_env,
            checker>::value;
};

template<class Sender>
constexpr bool has_error_code_v =
    has_error_code_completion<Sender>::value;

// Variant when sender can complete with
// set_error(error_code): separate slot so
// error_code is not wrapped in exception_ptr.
template<class ValueTuple>
using ec_result_variant = std::variant<
    std::monostate,
    ValueTuple,
    std::error_code,
    std::exception_ptr,
    stopped_t>;

// Variant when sender does not complete with
// set_error(error_code).
template<class ValueTuple>
using no_ec_result_variant = std::variant<
    std::monostate,
    ValueTuple,
    std::exception_ptr,
    stopped_t>;

template<class ValueTuple, bool HasEc>
using result_variant = std::conditional_t<
    HasEc,
    ec_result_variant<ValueTuple>,
    no_ec_result_variant<ValueTuple>>;

// Bridge receiver that stores the sender's
// completion result and resumes the coroutine.
// Uses an atomic flag shared with await_suspend
// to handle synchronous completion safely:
// whichever side (set_value or await_suspend)
// arrives second is responsible for resumption.
template<class ValueTuple, bool HasEc>
struct bridge_receiver
{
    using receiver_concept =
        stdexec::receiver_t;

    result_variant<ValueTuple, HasEc>* result_;
    std::coroutine_handle<>            cont_;
    std::stop_token                    st_;
    std::atomic<bool>*                 done_;

    auto get_env() const noexcept -> bridge_env
    {
        return {st_};
    }

    void resume_if_ready() noexcept
    {
        if(done_->exchange(
            true, std::memory_order_acq_rel))
            cont_.resume();
    }

    template<class... Args>
    void set_value(Args&&... args) && noexcept
    {
        result_->template emplace<1>(
            std::forward<Args>(args)...);
        resume_if_ready();
    }

    template<class E>
    void set_error(E&& e) && noexcept
    {
        if constexpr (
            HasEc &&
            std::is_same_v<
                std::decay_t<E>,
                std::error_code>)
            result_->template emplace<2>(
                std::forward<E>(e));
        else if constexpr (
            std::is_same_v<
                std::decay_t<E>,
                std::exception_ptr>)
        {
            constexpr auto idx = HasEc ? 3 : 2;
            result_->template emplace<idx>(
                std::forward<E>(e));
        }
        else
        {
            constexpr auto idx = HasEc ? 3 : 2;
            result_->template emplace<idx>(
                std::make_exception_ptr(
                    std::forward<E>(e)));
        }
        resume_if_ready();
    }

    void set_stopped() && noexcept
    {
        constexpr auto idx = HasEc ? 4 : 3;
        result_->template emplace<idx>(
            stopped_t{});
        resume_if_ready();
    }
};

} // namespace detail

/** Awaitable that bridges a stdexec sender
    into a Capy coroutine.

    Satisfies IoAwaitable. When co_awaited inside
    a capy::task, connects the sender to a bridge
    receiver, starts the operation, and resumes
    the coroutine when the sender completes.

    Uses an atomic exchange protocol to handle
    senders that complete synchronously during
    start(): whichever side arrives second
    (receiver or await_suspend) resumes the
    coroutine.

    The bridge inspects the sender's error
    completion signatures at compile time. If the
    sender can complete with
    set_error(std::error_code), await_resume
    returns io_result so the error code is a
    value, not an exception. Otherwise
    await_resume returns the value directly and
    genuine exceptions are rethrown.

    @tparam Sender The stdexec sender type.
*/
template<class Sender>
struct [[nodiscard]] sender_awaitable
{
    static constexpr bool has_ec =
        detail::has_error_code_v<Sender>;

    using value_tuple =
        detail::sender_single_value_t<Sender>;
    using variant_type =
        detail::result_variant<
            value_tuple, has_ec>;
    using receiver_type =
        detail::bridge_receiver<
            value_tuple, has_ec>;
    using op_state_type = decltype(
        stdexec::connect(
            std::declval<Sender>(),
            std::declval<receiver_type>()));

    Sender sndr_;
    variant_type result_{};

    alignas(op_state_type)
    unsigned char op_buf_[sizeof(op_state_type)];
    bool op_constructed_ = false;
    std::atomic<bool> done_{false};

    explicit sender_awaitable(Sender sndr)
        : sndr_(std::move(sndr))
    {
    }

    sender_awaitable(sender_awaitable&& o)
        noexcept(
            std::is_nothrow_move_constructible_v<
                Sender>)
        : sndr_(std::move(o.sndr_))
    {
    }

    sender_awaitable(
        sender_awaitable const&) = delete;
    sender_awaitable& operator=(
        sender_awaitable const&) = delete;
    sender_awaitable& operator=(
        sender_awaitable&&) = delete;

    ~sender_awaitable()
    {
        if(op_constructed_)
            std::launder(
                reinterpret_cast<op_state_type*>(
                    op_buf_))->~op_state_type();
    }

    bool await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<>
    await_suspend(
        std::coroutine_handle<> h,
        io_env const* env)
    {
        ::new(op_buf_) op_state_type(
            stdexec::connect(
                std::move(sndr_),
                receiver_type{
                    &result_, h,
                    env->stop_token, &done_}));
        op_constructed_ = true;
        stdexec::start(
            *std::launder(
                reinterpret_cast<
                    op_state_type*>(
                        op_buf_)));

        // If the sender completed during start(),
        // the receiver already stored the result.
        // Return h to resume without suspending.
        if(done_.exchange(
            true, std::memory_order_acq_rel))
            return h;
        return std::noop_coroutine();
    }

    auto await_resume()
    {
        if constexpr (has_ec)
            return await_resume_ec();
        else
            return await_resume_no_ec();
    }

private:
    // Sender can complete with
    // set_error(error_code). Return io_result
    // so the error code is a value, not an
    // exception.
    auto await_resume_ec()
    {
        // exception_ptr at index 3
        if(result_.index() == 3)
            std::rethrow_exception(
                std::get<3>(result_));

        if constexpr (
            std::tuple_size_v<
                value_tuple> == 0)
        {
            // stopped at index 4
            if(result_.index() == 4)
                return io_result<>{
                    make_error_code(
                        error::canceled)};
            if(result_.index() == 2)
                return io_result<>{
                    std::get<2>(result_)};
            return io_result<>{};
        }
        else if constexpr (
            std::tuple_size_v<
                value_tuple> == 1)
        {
            using T = std::tuple_element_t<
                0, value_tuple>;
            if(result_.index() == 4)
                return io_result<T>{
                    make_error_code(
                        error::canceled), T{}};
            if(result_.index() == 2)
                return io_result<T>{
                    std::get<2>(result_), T{}};
            return io_result<T>{
                {},
                std::get<0>(
                    std::get<1>(
                        std::move(result_)))};
        }
        else
        {
            if(result_.index() == 4)
                return io_result<value_tuple>{
                    make_error_code(
                        error::canceled), value_tuple{}};
            if(result_.index() == 2)
                return io_result<value_tuple>{
                    std::get<2>(result_), value_tuple{}};
            return io_result<value_tuple>{
                {},
                std::get<1>(
                    std::move(result_))};
        }
    }

    // Sender does not complete with
    // set_error(error_code). Return the value
    // directly; rethrow exceptions.
    auto await_resume_no_ec()
    {
        // exception_ptr at index 2
        if(result_.index() == 2)
            std::rethrow_exception(
                std::get<2>(result_));
        // stopped at index 3
        if(result_.index() == 3)
            throw detail::operation_cancelled{};

        if constexpr (
            std::tuple_size_v<
                value_tuple> == 0)
            return;
        else if constexpr (
            std::tuple_size_v<
                value_tuple> == 1)
            return std::get<0>(
                std::get<1>(
                    std::move(result_)));
        else
            return std::get<1>(
                std::move(result_));
    }
};

/** Create an IoAwaitable from a stdexec sender.

    If the sender can complete with
    set_error(std::error_code), the returned
    awaitable yields io_result so the error code
    is a value, not an exception. Otherwise the
    awaitable yields the value directly.

    @par Example
    @code
    capy::task<int> compute(auto sched)
    {
        auto result = co_await await_sender(
            stdexec::schedule(sched)
                | stdexec::then(
                    [] { return 42; }));
        co_return result;
    }
    @endcode

    @param sndr The sender to bridge.
    @return An IoAwaitable that can be co_awaited
        in a capy::task.
*/
template<class Sender>
auto await_sender(Sender&& sndr)
{
    return sender_awaitable<
        std::decay_t<Sender>>(
            std::forward<Sender>(sndr));
}

} // namespace boost::capy

#endif
