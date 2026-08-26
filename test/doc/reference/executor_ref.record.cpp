//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::executor_ref, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/executor_ref.hpp
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
// my_continuation must stay at a stable address until the executor
// dequeues it, so it is owned by the caller, not by store_executor().
void store_executor(capy::executor_ref ex, capy::continuation& my_continuation)
{
    if(ex)
        ex.post(my_continuation);
}

void use_thread_pool(capy::thread_pool& ctx, capy::continuation& my_continuation)
{
    store_executor(ctx.get_executor(), my_continuation);
}
// end::example[]
} // namespace ex_1

} // namespace
