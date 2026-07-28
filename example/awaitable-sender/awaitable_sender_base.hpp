//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXAMPLE_AWAITABLE_SENDER_BASE_HPP
#define BOOST_CAPY_EXAMPLE_AWAITABLE_SENDER_BASE_HPP

#include "awaitable_sender_detail.hpp"

#include <utility>

namespace boost::capy {

/** Concept for awaitables that are also senders.

    Refines @ref IoAwaitable with the `std::execution` sender
    requirements: the same op can be driven by `co_await` in a capy
    coroutine and by `connect`/`start` in a sender pipeline. Both
    halves are structural — deriving @ref awaitable_sender_base is
    the convenient way to satisfy the sender half, not the only way.

    @par Semantic Requirements
    The two protocols drive the identical awaitable members, so
    they agree only when the op reports its full disposition
    in-band through `await_resume()`, with cancellation expressed
    as an `error_code` comparing equal to
    `errc::operation_canceled` (see @ref awaitable_sender_base's
    Dual-Protocol Contract). Syntax alone cannot check this.

    @note Distinct from `std::execution::with_awaitable_senders`,
        which adapts in the opposite direction (senders made
        awaitable inside a coroutine).

    @tparam S The op type.
*/
template<class S>
concept AwaitableSender =
    IoAwaitable<S> &&
    ex::sender<S>;

/** CRTP mixin that makes an IoAwaitable a sender.

    Deriving from this base adds the C++26 sender interface to
    an I/O awaitable, so the same op type works under `co_await`
    in capy coroutines and under `connect`/`start` in sender
    pipelines. The completion signatures are deduced from
    `Derived::await_resume()` with the same rules as `as_sender`;
    deduction runs inside the signature query, where `Derived` is
    complete, so nothing is stated twice and the advertised
    signatures cannot drift from the implementation.

    @par Dual-Protocol Contract
    Both protocols drive the identical awaitable members, so the
    two views cannot diverge as long as `Derived` reports its full
    disposition in-band: `await_resume()`'s result carries the
    outcome, with cancellation expressed as an `error_code`
    comparing equal to `errc::operation_canceled`. The sender
    machinery derives completion channels from that result alone.
    Results without an error channel (`void`, plain values) are
    dual-use-safe only for ops that cannot be canceled.

    @note Derived ops should keep their data members private.
        The standard imposes no access requirements on senders,
        but `std::execution` sender decomposition claims any type
        whose members admit a structured binding and treats the
        first member as a sender tag; private members make the
        binding ill-formed, so the op is categorically
        non-decomposable and independent of the dispatch
        machinery's guarded fallbacks.

    @par Example
    @code
    struct read_op : awaitable_sender_base<read_op>
    {
        // usual IoAwaitable members; the completion signatures
        // follow await_resume()'s result type
    };
    @endcode

    @tparam Derived The op type deriving from this base.
*/
template<class Derived>
struct awaitable_sender_base
{
    using sender_concept = ex::sender_t;

    /** Return the completion signatures deduced from `Derived`.

        This is the C++26 mechanism: a static member function
        template the implementation calls as
        `Sndr::template get_completion_signatures<Sndr, Env...>()`
        ([exec.getcomplsigs]; P3164R4 also removed the
        nested-typedef mechanism from the standard).
    */
    template<class Sndr, class... Env>
    static consteval auto get_completion_signatures() noexcept
    {
        return decltype(detail::make_sigs<Derived>()){};
    }

    /// Connect the op to a receiver, consuming it.
    template<class Receiver>
    auto connect(Receiver rcvr) &&
        -> detail::awaitable_op_state<Derived, Receiver>
    {
        return detail::awaitable_op_state<Derived, Receiver>(
            static_cast<Derived&&>(*this), std::move(rcvr));
    }

    /// Connect a copy of the op to a receiver.
    template<class Receiver>
    auto connect(Receiver rcvr) const&
        -> detail::awaitable_op_state<Derived, Receiver>
    {
        return detail::awaitable_op_state<Derived, Receiver>(
            static_cast<Derived const&>(*this),
            std::move(rcvr));
    }
};

} // namespace boost::capy

#endif
