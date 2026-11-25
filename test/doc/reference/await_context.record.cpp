//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::await_context, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/await_context.hpp
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
// An awaitable keeps the pointer, not a copy, and queues the
// coroutine for resumption through env->executor.
struct my_awaitable
{
    capy::await_context const* env_ = nullptr;
    capy::continuation cont_;

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h, capy::await_context const* env)
    {
        env_ = env;
        cont_.h = h;
        cont_.owner = env->owner;
        env->executor.post(cont_);
        return std::noop_coroutine();
    }

    void await_resume() noexcept {}
};
// end::example[]

static_assert( capy::ContextAwaitable<my_awaitable> );
} // namespace ex_1

} // namespace
