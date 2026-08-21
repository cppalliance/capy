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

// Examples deliberately leave results unused; the reference explains the
// values in prose instead.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#if defined(_MSC_VER)
#pragma warning(disable: 4189) // local variable initialized but not referenced
#pragma warning(disable: 4101) // unreferenced local variable
#pragma warning(disable: 4505) // unreferenced local function removed
#endif

#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/ex/work_guard.hpp>

namespace capy = boost::capy;

namespace {

using namespace boost::capy;

// tag::work_guard[]
void keep_alive_while_setting_up()
{
    thread_pool pool(1);

    // Keep the pool from completing while we set things up. Note
    // make_work_guard() takes the Executor from get_executor(), not
    // the thread_pool context itself.
    auto guard = make_work_guard(pool.get_executor());

    // ... post work to pool ...

    // Allow the pool to complete when work is done
    guard.reset();

    pool.join();
}
// end::work_guard[]

} // namespace
