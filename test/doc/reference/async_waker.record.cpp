//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::async_waker, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/async_waker.hpp
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
void waker_example()
{
    capy::async_waker waker;

    // user-provided timing thread
    std::thread th([&waker] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waker.wake();
    });

    auto waiter = [&waker]() -> capy::task<> {
        auto [ec] = co_await waker.wait();
        // resumed on the executor after ~100ms
    };

    // ... run waiter() on an executor and let the pool drain

    th.join();  // waker.wake() has run; safe to destroy waker now
}
// end::example[]
} // namespace ex_1

} // namespace
