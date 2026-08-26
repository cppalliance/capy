//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/2.cpp20-coroutines/2c.machinery.adoc.
// Declaration-only scaffolding; compiling is the test.

#include "../doc_warnings.hpp"

#include <coroutine>

namespace {

// Minimal return type showing how get_return_object obtains a handle
struct handle_demo
{
    struct promise_type
    {
        handle_demo get_return_object()
        {
            auto h =
                // tag::from_promise[]
                std::coroutine_handle<promise_type>::from_promise(*this)
                // end::from_promise[]
                ;
            return handle_demo{h};
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };

    std::coroutine_handle<promise_type> handle;
};

[[maybe_unused]] handle_demo make_handle_demo()
{
    co_return;
}

} // namespace
