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

#include "awaitable_sender_base.hpp"
#include "awaitable_sender_detail.hpp"

#include <boost/capy/concept/io_awaitable.hpp>

#include <beman/execution/execution.hpp>

#include <type_traits>
#include <utility>

namespace boost::capy {

// -------------------------------------------------------
// Sender that wraps an IoAwaitable
// -------------------------------------------------------

/** A sender that wraps an IoAwaitable.

    Adapts an IoAwaitable to the `std::execution` sender
    concept, enabling composition with other sender operations
    and adapters.
*/
template<class IoAw>
struct awaitable_sender
{
    using sender_concept = ex::sender_t;

    IoAw aw_;

    /** Return the completion signatures deduced from `IoAw`.

        C++26 static-template form (see @ref awaitable_sender_base
        for the mechanism notes).
    */
    template<class Sndr, class... Env>
    static consteval auto get_completion_signatures() noexcept
    {
        return decltype(detail::make_sigs<IoAw>()){};
    }

    /// beman-compat form: DROP AT GRADUATION (pre-P3164 protocol).
    template<class Env>
    constexpr auto get_completion_signatures(
        Env const&) const noexcept
    {
        return decltype(detail::make_sigs<IoAw>()){};
    }

    /// Connect this sender with a receiver to form an operation state.
    template<class Receiver>
    auto connect(Receiver rcvr) &&
        -> detail::awaitable_op_state<IoAw, Receiver>
    {
        return detail::awaitable_op_state<IoAw, Receiver>(
            std::move(aw_), std::move(rcvr));
    }

    /// Connect a copy of the sender to a receiver.
    template<class Receiver>
    auto connect(Receiver rcvr) const&
        -> detail::awaitable_op_state<IoAw, Receiver>
    {
        return detail::awaitable_op_state<IoAw, Receiver>(
            aw_, std::move(rcvr));
    }
};

/** Create a `std::execution` sender from an IoAwaitable.

    The bridge routes the awaitable's result through sender
    channels based on its type:

    - `void` - calls `set_value()`.
    - `error_code` or an empty `io_result` - calls
      `set_value()` when the code is zero, `set_error(ec)`
      otherwise.
    - `io_result<Ts...>` with payload elements - calls
      `set_value(ts...)` when `ec` is zero, `set_error(ec)`
      otherwise. Any partial payload accompanying a truthy
      `ec` is dropped, since sender completion channels are
      exclusive.
    - Any other single value `T` - calls `set_value(T)`,
      including generic tuple-likes that happen to lead with
      an `error_code`: only `io_result` declares the
      element-0-is-outcome intent, so only it is split.

    For the `error_code`-carrying result types the channel is
    chosen by the operation's own disposition: an `ec` that
    compares equal to `errc::operation_canceled` completes with
    `set_stopped()`, and a successful result is delivered even
    if a stop request arrived while the operation was finishing.
    `void` and plain-value results carry no disposition, so for
    those the environment's stop token decides between
    `set_stopped()` and the completion above.

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
    return awaitable_sender<std::decay_t<IoAw>>{
        std::forward<IoAw>(aw)};
}

/** Return the awaitable as a sender.

    Boundary normalizer for generic code handed an arbitrary
    @ref IoAwaitable: an op that already models
    @ref AwaitableSender passes through unchanged, anything else
    is lifted with @ref as_sender. Either way the result is a
    sender by value.

    @param a The IoAwaitable to normalize.
    @return `a` itself, or `as_sender(a)`.
*/
template<IoAwaitable A>
auto ensure_sender(A&& a)
{
    if constexpr (AwaitableSender<std::remove_cvref_t<A>>)
        return std::forward<A>(a);
    else
        return as_sender(std::forward<A>(a));
}

// -------------------------------------------------------
// split_ec: sender adapter that routes error_code to
// set_value() or set_error(ec) at runtime.
// -------------------------------------------------------

namespace detail {

template<class Sender>
struct split_ec_sender
{
    using sender_concept = ex::sender_t;

    using sigs_type =
        ex::completion_signatures<
            ex::set_value_t(),
            ex::set_error_t(std::error_code),
            ex::set_error_t(std::exception_ptr),
            ex::set_stopped_t()>;

    Sender sndr_;

    // C++26 static-template form plus the beman-compat instance
    // form (DROP the latter at graduation; pre-P3164 protocol).
    template<class Sndr, class... Env>
    static consteval auto get_completion_signatures() noexcept
    {
        return sigs_type{};
    }

    template<class Env>
    constexpr auto get_completion_signatures(
        Env const&) const noexcept
    {
        return sigs_type{};
    }

    template<class Receiver>
    struct ec_receiver
    {
        using receiver_concept = ex::receiver_t;

        Receiver rcvr_;

        auto get_env() const noexcept
        {
            return ex::get_env(rcvr_);
        }

        void set_value(std::error_code ec) && noexcept
        {
            if (!ec)
                ex::set_value(
                    std::move(rcvr_));
            else
                ex::set_error(
                    std::move(rcvr_), ec);
        }

        void set_value() && noexcept
        {
            ex::set_value(
                std::move(rcvr_));
        }

        template<class E>
        void set_error(E&& e) && noexcept
        {
            ex::set_error(
                std::move(rcvr_),
                std::forward<E>(e));
        }

        void set_stopped() && noexcept
        {
            ex::set_stopped(
                std::move(rcvr_));
        }
    };

    template<class Receiver>
    struct op_state
    {
        using operation_state_concept =
            ex::operation_state_t;

        using inner_op_t = decltype(
            ex::connect(
                std::declval<Sender>(),
                std::declval<ec_receiver<Receiver>>()));

        inner_op_t op_;

        op_state(Sender sndr, Receiver rcvr)
            : op_(ex::connect(
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
            ex::start(op_);
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
