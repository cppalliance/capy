//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::any_executor, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/any_executor.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

#include "../doc_warnings.hpp"

#include <boost/capy.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace capy = boost::capy;

namespace {

namespace ex_1 {
// tag::example[]
// c must stay at a stable address until the executor dequeues it, so
// the caller owns it -- typically as part of the awaitable or
// operation state that posts it, never as a callee-local temporary.
void dispatch_via_context(capy::thread_pool& ctx, capy::continuation& c)
{
    capy::any_executor exec = ctx.get_executor();
    if(exec)
    {
        auto& context = exec.context();
        // dispatch() may hand the continuation straight back for
        // symmetric transfer instead of enqueuing it, so the returned
        // handle must be resumed or the continuation is dropped.
        exec.dispatch(c).resume();
    }
}
// end::example[]
} // namespace ex_1

} // namespace
