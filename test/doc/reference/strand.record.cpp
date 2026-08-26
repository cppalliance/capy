//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::strand, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/strand.hpp
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
// Continuations are linked intrusively into the strand's queue, so
// each must outlive its time there. The caller owns c1/c2/c3 --
// typically as members of the awaitable or operation state that
// posts them, never as locals that go out of scope while enqueued.
void post_three(capy::thread_pool& pool,
    capy::continuation& c1, capy::continuation& c2, capy::continuation& c3)
{
    capy::strand sd(pool.get_executor());  // CTAD deduces the executor type

    sd.post(c1);
    sd.post(c2);
    sd.post(c3);
}
// end::example[]
} // namespace ex_1

} // namespace
