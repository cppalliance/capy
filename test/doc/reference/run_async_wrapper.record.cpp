//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::run_async_wrapper, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/run_async.hpp
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
capy::task<void> my_task() { co_return; }

void correct_usage( capy::any_executor ex )
{
    // Correct usage - wrapper is temporary, task is the direct argument
    capy::run_async(ex)(my_task());
}

void rvalue_only_call( capy::any_executor ex )
{
    // Compiles - copy elision constructs w directly from the prvalue
    auto w = capy::run_async(ex);

    // Calling on the rvalue is the supported form; calling through
    // the stored lvalue is rejected by the rvalue ref-qualifier.
    using wrapper = decltype(w);
    static_assert(   std::is_invocable_v< wrapper,  capy::task<void> > );
    static_assert( ! std::is_invocable_v< wrapper&, capy::task<void> > );

    std::move(w)(my_task());  // Compiles: w is now an rvalue
}

void silent_misuse( capy::any_executor ex )
{
    // Compiles, but WRONG - task frame allocated before run_async runs
    auto t = my_task();
    capy::run_async(ex)(std::move(t));
}
// end::example[]
} // namespace ex_1

} // namespace
