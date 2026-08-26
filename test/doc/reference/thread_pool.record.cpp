//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::thread_pool, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/thread_pool.hpp
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
capy::task<void> some_task() { co_return; }

void run_on_pool()
{
    capy::thread_pool pool(4);  // 4 worker threads
    auto ex = pool.get_executor();
    capy::run_async(ex)(some_task());  // start work; tracked so join() waits for it
    pool.join();  // wait for outstanding work to complete
    // pool destructor stops the pool, discarding any pending work
}
// end::example[]
} // namespace ex_1

} // namespace
