//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples injected into include/boost/capy/ex/work_guard.hpp's
// documentation by doc/addons/extensions/reference-snippets.lua. The tagged
// region is what the reference renders; scaffolding stays outside the tags.

#include "../doc_warnings.hpp"

// This example defines a helper that nothing calls; MSVC reports the removal.
#if defined(_MSC_VER)
#pragma warning(disable: 4505) // unreferenced local function removed
#endif

#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/ex/work_guard.hpp>

namespace capy = boost::capy;

namespace {


// tag::work_guard[]
void keep_alive_while_setting_up()
{
    capy::thread_pool pool(1);

    // Keep the pool from completing while we set things up. Note
    // make_work_guard() takes the Executor from get_executor(), not
    // the thread_pool context itself.
    auto guard = capy::make_work_guard(pool.get_executor());

    // ... post work to pool ...

    // Allow the pool to complete when work is done
    guard.reset();

    pool.join();
}
// end::work_guard[]

} // namespace
