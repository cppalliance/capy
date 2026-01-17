//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
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

namespace boost {
namespace capy {

/** Concept for executor types.

    An executor provides mechanisms for scheduling work for
    execution. A type meeting the executor requirements embodies
    a set of rules for determining how submitted function objects
    are to be executed.

    @par Required Operations

    @li `context()` - Returns a reference to the associated
        execution context.

    @li `on_work_started()` - Informs the executor that work is
        beginning. Must be paired with `on_work_finished()`.

    @li `on_work_finished()` - Informs the executor that work has
        completed. Precondition: a preceding call to
        `on_work_started()` on an equal executor.

    @li `operator()(h)` - Execute a coroutine, potentially immediately
        if the executor determines it is safe to do so. The executor
        may block forward progress of the caller until execution
        completes. This also serves as the dispatcher interface.

    @li `post(h)` - Queue a coroutine for later execution. The
        executor shall not block forward progress of the caller
        pending completion.

    @li `defer(h)` - Queue a coroutine for later execution. The
        executor shall not block forward progress of the caller
        pending completion. Semantically identical to `post`, but
        conveys a preference that the coroutine is a continuation
        of the current call context. The executor may use this
        information to optimize or otherwise adjust invocation.

    @par Synchronization

    The invocation of `dispatch`, `post`, or `defer` synchronizes
    with the invocation of the coroutine.

    @par No-Throw Guarantee

    The following operations shall not exit via an exception:
    constructors, comparison operators, copy/move operations,
    swap, `context()`, `on_work_started()`, and `on_work_finished()`.

    @par Thread Safety

    The executor copy constructor, comparison operators, and other
    member functions shall not introduce data races as a result of
    concurrent calls from different threads.

    @par Executor Validity

    Let `ctx` be the execution context returned by `context()`.
    An executor becomes invalid when the first call to
    `ctx.shutdown()` returns. The effect of calling
    `on_work_started`, `on_work_finished`, `dispatch`, `post`,
    or `defer` on an invalid executor is undefined.

    @note The copy constructor, comparison operators, and `context()`
    remain valid until `ctx` is destroyed.

    @tparam E The type to check for executor conformance.
*/
template<class E>
concept executor =
    std::copy_constructible<E> &&
    std::equality_comparable<E> &&
    requires(E& e, E const& ce, std::coroutine_handle<> h) {
        // Execution context access (must not throw)
        { ce.context() } noexcept -> std::same_as<decltype(ce.context())&>;

        // Work tracking (must not throw)
        { ce.on_work_started() } noexcept;
        { ce.on_work_finished() } noexcept;

        // Work submission
        { ce(h) } -> std::convertible_to<std::coroutine_handle<>>;
        { ce.post(h) };
        { ce.defer(h) };
    };

} // capy
} // boost

#endif
