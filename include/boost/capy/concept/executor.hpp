//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_EXECUTOR_HPP
#define BOOST_CAPY_CONCEPT_EXECUTOR_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/continuation.hpp>

#include <concepts>
#include <coroutine>
#include <type_traits>

namespace boost {
namespace capy {

class execution_context;

/** Requires a copyable type to dispatch or post continuations to its context.

    An executor embodies a set of rules for determining how and where
    coroutines are executed. It provides operations to submit work
    and to track outstanding work for graceful shutdown.

    Ordinary users writing coroutine tasks do not interact with
    `dispatch` and `post` directly. These operations are used by
    authors of coroutine machinery -- `promise_type` implementations,
    awaitables, `await_transform`. That code implements asynchronous
    algorithms such as `when_all`, `when_any`, `async_mutex`,
    channels, and similar primitives.

    @tparam E The executor type.

    @par Syntactic Requirements

    @li `E` must be nothrow copy and move constructible.
    @li `ce == ce2` must return a type convertible to `bool`, `noexcept`.
    @li `ce.context()` must return an lvalue reference to a type derived
        from `execution_context`, `noexcept`.
    @li `ce.on_work_started()` must be valid and `noexcept`.
    @li `ce.on_work_finished()` must be valid and `noexcept`.
    @li `ce.dispatch(c)` must return `std::coroutine_handle<>`.
    @li `ce.post(c)` must be valid.

    @par Semantic Requirements

    The `context` operation returns the owning context:

    @li Returns a reference to the execution context that created
        this executor.
    @li The context outlives all executors created from it.

    The `on_work_started` and `on_work_finished` operations track work:

    @li Calls must be paired; each `on_work_started` must have a
        matching `on_work_finished`.
    @li The context uses this count to determine when shutdown
        is complete.
    @li These are not intended for direct use by callers. They
        are public so that work guards can invoke them. This
        enables user-defined guards with additional tracking
        behaviors, without the library needing to grant friendship
        to types it cannot anticipate.

    The `dispatch` operation returns a handle for symmetric transfer:

    Every coroutine resumption must go through symmetric transfer
    or the executor's queue. It must never go through an inline
    `resume()` or `dispatch()` that creates a frame below the
    resumed coroutine.

    @li If the executor determines it is safe to resume inline,
        returns `c.h` for the caller to use in symmetric transfer.
        One such case is an executor already on the correct thread.
    @li Otherwise, posts the continuation for later execution and
        returns `std::noop_coroutine()`.
    @li The caller must use the returned handle appropriately.
        Return it from `await_suspend` for symmetric transfer, or
        call `.resume()` if at the event loop pump level.

    A conforming implementation might look like:

    @par !example example_1


    The `post` operation queues for later execution:

    @li Never blocks the caller.
    @li The coroutine executes on the executor's associated context.

    @par Continuation Lifetime

    Both `dispatch` and `post` operate on the caller's
    continuation object by reference. The continuation must
    remain at a stable address and must not be moved or
    destroyed until the executor has dequeued and resumed it.
    Destroying or moving a continuation while it is linked
    into an executor's queue is undefined behavior.

    When `dispatch` returns `c.h` (the inline case), the
    continuation is not enqueued and may be reused or
    destroyed immediately. When `dispatch` falls through to
    `post`, the continuation is enqueued and the lifetime
    requirement applies.

    @par Frame Allocator TLS

    The library propagates a frame allocator via thread-local
    storage. When a custom executor's event loop calls
    `.resume()` to drain its work queue, it must use
    `safe_resume()` from `<boost/capy/ex/frame_allocator.hpp>`
    instead of calling `h.resume()` directly. This saves and
    restores the thread-local frame allocator around the call,
    preventing a resumed coroutine from permanently overwriting
    the caller's value.

    @par Executor Validity

    Once `ctx.shutdown()` has begun, the context runs no more work.
    `dispatch` and `post` remain valid until the context is destroyed,
    but never run the coroutine they are given: the executor passes
    the continuation to @ref abandon, either at once or no later than
    the context's destruction, as a context does with the coroutines
    it holds. An executor takes a continuation off its queue before it
    resumes or abandons it. A queued copy keeps the continuation's
    `owner`, which @ref abandon needs. An abandon made inside `post` may
    unwind the chain before `post` returns, so it is the executor's last
    touch of the continuation, and of the executor object itself, which
    may live in a frame the unwind destroys. A `post` that throws has
    neither queued nor abandoned the continuation; the caller still
    holds it. Copy, comparison, `context()`,
    `on_work_started`, and `on_work_finished` also remain valid until
    the context is destroyed. Calling any member after the context is
    destroyed is undefined behavior.

    @par Throwing `post` or `dispatch`

    An exception from `post` or `dispatch` reaches the caller only
    where nothing has started yet: @ref run_async rethrows it after
    destroying the unstarted task. Everywhere else the library calls
    them where an exception cannot propagate, and `std::terminate` is
    called: while a @ref task is suspending, which includes
    @ref when_all and @ref when_any starting their children and
    @ref run moving to its executor; when @ref abandon posts an
    unwind; when a join or a @ref run boundary resumes the
    coroutine awaiting it; and when @ref async_mutex, @ref async_event
    or @ref async_waker resumes a waiter, whether from an unlock, a
    set, a wake, a stop request or its own destruction.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Safe for copy, comparison, and `context()`.

    @par Conforming Signatures

    @par !example example_2


    @see ExecutionContext, execution_context
*/
// tag::executor_concept[]
template<class E>
concept Executor =
    std::is_nothrow_copy_constructible_v<E> &&
    std::is_nothrow_move_constructible_v<E> &&
    requires(E& e, E const& ce, E const& ce2, continuation c) {
        { ce == ce2 } noexcept -> std::convertible_to<bool>;
        { ce.context() } noexcept;
        requires std::is_lvalue_reference_v<decltype(ce.context())> &&
            std::derived_from<
                std::remove_reference_t<decltype(ce.context())>,
                execution_context>;
        { ce.on_work_started() } noexcept;
        { ce.on_work_finished() } noexcept;

        { ce.dispatch(c) } -> std::same_as<std::coroutine_handle<>>;
        { ce.post(c) };
    };
// end::executor_concept[]

} // capy
} // boost

#endif
