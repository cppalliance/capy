//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::thread_pool::executor_type, injected
// into its documentation by doc/addons/extensions/reference-snippets.lua.
// Declared in:
//   include/boost/capy/ex/thread_pool.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

#include "../doc_warnings.hpp"

#include <boost/capy.hpp>

#include <cassert>

namespace capy = boost::capy;

namespace {

namespace ex_1 {
// tag::example[]
capy::task<> my_task() { co_return; }

void copy_executor()
{
    capy::thread_pool pool(2);
    capy::thread_pool::executor_type ex = pool.get_executor();
    auto copy = ex;                 // refers to the same pool
    assert(copy == ex);
    capy::run_async(ex)(my_task()); // my_task runs on a pool thread
    pool.join();
}
// end::example[]
} // namespace ex_1

} // namespace
