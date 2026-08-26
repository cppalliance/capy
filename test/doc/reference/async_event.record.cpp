//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::async_event, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/async_event.hpp
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
capy::async_event event;

capy::task<> waiter() {
    auto [ec] = co_await event.wait();
    if(ec)
        co_return;
    // ... event was set ...
}

capy::task<> notifier() {
    // ... do some work ...
    event.set();  // Wake all waiters
    co_return;
}
// end::example[]
} // namespace ex_1

} // namespace
