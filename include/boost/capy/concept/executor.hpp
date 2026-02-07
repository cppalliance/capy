//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_EXECUTOR_HPP
#define BOOST_CAPY_CONCEPT_EXECUTOR_HPP

#include <boost/capy/detail/config.hpp>

#include <concepts>
#include <coroutine>
#include <type_traits>

namespace boost {
namespace capy {

class execution_context;

/** Concept for types that schedule coroutine execution.

    An executor embodies a set of rules for determining how and where
    coroutines are executed. It provides operations to submit work
    and to track outstanding work for graceful shutdown.

    @tparam E The executor type.

    @par Syntactic Requirements

    @li `E` must be nothrow copy and move constructible
    @li `e1 == e2` must return a type convertible to `bool`, `noexcept`
    @li `e.context()` must return an lvalue reference to a type derived
        from `execution_context`, `noexcept`
    @li `e.on_work_started()` must be valid and `noexcept`
    @li `e.on_work_finished()` must be valid and `noexcept`
    @li `e.dispatch(h)` must be a valid expression
    @li `e.post(h)` must be valid

    @par Semantic Requirements

    The `context` operation returns the owning context:

    @li Returns a reference to the execution context that created
        this executor
    @li The context outlives all executors created from it

    The `on_work_started` and `on_work_finished` operations track work:

    @li Calls must be paired; each `on_work_started` must have a
        matching `on_work_finished`
    @li The context uses this count to determine when shutdown
        is complete

    The `dispatch` operation executes immediately if safe:

    @li If the executor determines it is safe (e.g., already on the
        correct thread), resumes the coroutine inline via a normal
        function call
    @li The call returns when the coroutine suspends or completes
    @li If not safe, posts the coroutine for later execution

    A conforming implementation might look like:

    @code
    void dispatch( std::coroutine_handle<> h ) const
    {
        if( ctx_.is_running_on_this_thread() )
            h.resume();  // inline execution
        else
            post( h );   // deferred execution
    }
    @endcode

    After `dispatch(h)` returns, the state of `h` is unspecified.
    The coroutine may have completed, been destroyed, or suspended
    at a different suspension point. Callers must not assume `h`
    remains valid or in its original state after calling `dispatch`.

    @par Note
    Because `dispatch` may call `h.resume()` before returning,
    it cannot be used to implement symmetric transfer from
    `await_suspend`. Patterns like `return ex.dispatch(h)` are
    ill-formed since `dispatch` returns `void`, and even if it
    returned the handle, the coroutine would have already been
    resumed, leading to undefined behavior.

    The `post` operation queues for later execution:

    @li Never blocks the caller
    @li The coroutine executes on the executor's associated context

    @par Executor Validity

    An executor becomes invalid when the first call to
    `ctx.shutdown()` returns. Calling `dispatch`, `post`,
    `on_work_started`, or `on_work_finished` on an invalid executor
    is undefined behavior. Copy, comparison, and `context()` remain
    valid until the context is destroyed.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Safe for copy, comparison, and `context()`.

    @par Conforming Signatures

    @code
    class E
    {
    public:
        execution_context& context() const noexcept;

        void on_work_started() const noexcept;
        void on_work_finished() const noexcept;

        void dispatch( std::coroutine_handle<> h ) const;
        void post( std::coroutine_handle<> h ) const;

        bool operator==( E const& ) const noexcept;
    };
    @endcode

    @par Example

    @code
    template<Executor Ex>
    void submit_work( Ex ex, std::coroutine_handle<> h )
    {
        ex.on_work_started();
        ex.post( h );
        // on_work_finished called when coroutine completes
    }
    @endcode

    @see ExecutionContext, execution_context
*/
template<class E>
concept Executor =
    std::is_nothrow_copy_constructible_v<E> &&
    std::is_nothrow_move_constructible_v<E> &&
    requires(E& e, E const& ce, E const& ce2, std::coroutine_handle<> h) {
        { ce == ce2 } noexcept -> std::convertible_to<bool>;
        { ce.context() } noexcept;
        requires std::is_lvalue_reference_v<decltype(ce.context())> &&
            std::derived_from<
                std::remove_reference_t<decltype(ce.context())>,
                execution_context>;
        { ce.on_work_started() } noexcept;
        { ce.on_work_finished() } noexcept;

        { ce.dispatch(h) };
        { ce.post(h) };
    };

} // capy
} // boost

#endif
