//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
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

/** Concept for types that provide a place where work is executed.

    An execution context owns the resources (threads, event loops,
    completion ports) needed to execute function objects. It serves
    as the factory for executors, which are lightweight handles used
    to submit work. Multiple executors may reference the same context.

    @tparam X The execution context type.

    @par Syntactic Requirements

    @li `X` must be publicly derived from `execution_context`
    @li `X::executor_type` must be a type satisfying @ref Executor
    @li `x.get_executor()` must return `X::executor_type` and be `noexcept`

    @par Semantic Requirements

    The execution context owns the execution environment:

    @li Work submitted via any executor from this context runs on
        resources owned by the context
    @li The context remains valid while any executor referencing it
        exists and may be used
    @li Destroying the context destroys all unexecuted work submitted
        via associated executors

    @par Conforming Signatures

    @code
    class X : public execution_context
    {
    public:
        using executor_type = // Executor
        executor_type get_executor() noexcept;
    };
    @endcode

    @par Example

    @code
    template<ExecutionContext Ctx>
    void spawn_work( Ctx& ctx )
    {
        auto ex = ctx.get_executor();
        ex.post( []{ } ); // work runs on ctx
    }
    @endcode

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
