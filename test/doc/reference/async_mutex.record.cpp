//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::async_mutex, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/async_mutex.hpp
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
capy::async_mutex cm;

capy::task<> protected_operation() {
    auto [ec] = co_await cm.lock();
    if(ec)
        co_return;
    // ... critical section ...
    cm.unlock();
}

// Or with RAII:
capy::task<> protected_operation_raii() {
    auto [ec, guard] = co_await cm.scoped_lock();
    if(ec)
        co_return;
    // ... critical section ...
    // unlocks automatically
}
// end::example[]
} // namespace ex_1

} // namespace
