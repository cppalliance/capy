//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/9.design/9m.WhyNotCobalt.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/ex/io_env.hpp>

#include <coroutine>

namespace capy = boost::capy;

namespace {

// Shows the IoAwaitable protocol signature used for context propagation.
struct context_propagating_op
{
    // tag::await_suspend_env[]
    auto await_suspend(std::coroutine_handle<> h, capy::io_env const* env);
    // end::await_suspend_env[]
};

} // namespace
