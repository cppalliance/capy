//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_AWAIT_CONTEXT_HPP
#define BOOST_CAPY_AWAIT_CONTEXT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <coroutine>
#include <memory_resource>
#include <stop_token>

namespace boost {
namespace capy {

class chain_owner;

/** Carries the executor, stop token, and frame allocator through a coroutine chain.

    This struct bundles the execution context passed through
    coroutine chains via the ContextAwaitable protocol. It contains
    the executor for resumption, a stop token for cancellation,
    and an optional frame allocator for coroutine frame allocation.

    @par Lifetime

    Launcher functions (@ref run_async, @ref run) own the `await_context` and
    guarantee it outlives all tasks and awaitables in the started
    chain. Awaitables receive `await_context const*` in `await_suspend`
    and should store it directly, never copy the pointed-to object.

    @par Thread Safety
    The referenced executor and allocator must remain valid
    for the lifetime of any coroutine using this environment.

    @see @ref ContextAwaitable, @ref ContextRunnable
*/
struct await_context
{
    /** The executor for coroutine resumption. */
    executor_ref executor;

    /** The stop token for cancellation propagation. */
    std::stop_token stop_token;

    /** The frame allocator for coroutine frame allocation.

        When null, the default allocator is used.
    */
    std::pmr::memory_resource* frame_allocator = nullptr;

    /** The owner of the chain segment running in this environment.

        Set by whatever started the segment (`run_async`, `run` on
        another executor, a `when_all` or `when_any` branch). An
        awaitable that parks or queues a coroutine copies it into the
        @ref continuation it keeps, so a holder can @ref abandon the
        segment. Null when nothing owns the segment.

        A launcher that starts a task under an environment it builds
        itself, rather than through `run_async`, `run` or a join, must
        either own the segment, binding a @ref chain_owner, or ensure
        nothing awaits the coroutines that could be abandoned. With no
        owner, @ref abandon destroys only the frame it is given, and any
        frame awaiting it is left holding a destroyed frame.
    */
    chain_owner* owner = nullptr;

};

} // capy
} // boost

#endif
