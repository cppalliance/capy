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

/** Concept for execution context types.

    An execution context represents a place where function objects
    are executed. A type meeting the ExecutionContext requirements
    must be publicly derived from execution_context and provide
    an associated executor type.

    @par Required Operations

    @li `X::executor_type` - A type meeting the Executor requirements.

    @li `x.get_executor()` - Returns an executor object associated
        with the execution context.

    @par Destructor Semantics

    The destructor destroys all unexecuted function objects that
    were submitted via an executor associated with this context.

    @tparam X The type to check for execution context conformance.
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
