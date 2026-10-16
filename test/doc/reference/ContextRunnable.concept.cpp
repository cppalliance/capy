//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::ContextRunnable, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/concept/context_runnable.hpp
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
class T
{
public:
    struct promise_type
    {
        std::exception_ptr exception() noexcept;
        int result();  // non-void tasks only
        void set_continuation(std::coroutine_handle<>) noexcept;
        void set_environment(capy::await_context const*) noexcept;
    };

    std::coroutine_handle<promise_type> handle() const noexcept;
    void release() noexcept;
};
// end::example[]
} // namespace ex_1

} // namespace
