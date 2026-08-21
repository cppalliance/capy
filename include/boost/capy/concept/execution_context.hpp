//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_EXECUTION_CONTEXT_HPP
#define BOOST_CAPY_CONCEPT_EXECUTION_CONTEXT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/execution_context.hpp>

#include <concepts>

namespace boost {
namespace capy {

/** Requires a type to expose a `noexcept get_executor()` bound to its own resources.

    An execution context owns the resources (threads, event loops,
    completion ports) needed to run coroutine continuations. It serves
    as the factory for executors, which are lightweight handles used
    to submit work. Multiple executors may reference the same context.

    @tparam X The execution context type.

    @par Syntactic Requirements

    @li `X` must be publicly derived from `execution_context`.
    @li `X::executor_type` must be a type satisfying @ref Executor.
    @li `x.get_executor()` must return `X::executor_type` and be `noexcept`.

    @par Semantic Requirements

    The execution context owns the execution environment:

    @li Work submitted via any executor from this context runs on
        resources owned by the context.
    @li The context remains valid while any executor referencing it
        exists and may be used.
    @li Destroying the context abandons work submitted via associated
        executors that has not started running.

    @par Conforming Signatures

    @par !example example_1


    @par Example

    `post` takes a `continuation&`, which no closure converts to; ordinary
    callers reach it indirectly through `run_async` or similar combinators:

    @par !example example_2


    @see Executor, execution_context
*/
template<class X>
concept ExecutionContext =
    std::derived_from<X, execution_context> &&
    requires(X& x) {
        typename X::executor_type;
        requires Executor<typename X::executor_type>;
        { x.get_executor() } noexcept -> std::same_as<typename X::executor_type>;
    };

} // capy
} // boost

#endif
