//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::continuation, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/continuation.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

#include "../doc_warnings.hpp"

#include <boost/capy.hpp>

#include <coroutine>

namespace capy = boost::capy;

namespace {

namespace ex_1 {
// tag::example[]
// The awaitable embeds the continuation so its address stays stable
struct yield_awaitable
{
    capy::continuation c;

    bool await_ready() const noexcept { return false; }

    void await_suspend(
        std::coroutine_handle<> h, capy::await_context const* env )
    {
        c.h = h;
        c.owner = env->owner; // lets a teardown abandon the whole segment
        env->executor.post( c );
    }

    void await_resume() const noexcept {}
};
// end::example[]

static_assert( capy::ContextAwaitable<yield_awaitable> );
} // namespace ex_1

} // namespace
