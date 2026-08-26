//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::ExecutionContext, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/concept/execution_context.hpp
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
// tag::example_1[]
class X : public capy::execution_context
{
public:
    using executor_type = capy::executor_ref;  // any type satisfying Executor
    executor_type get_executor() noexcept;
};

static_assert( capy::ExecutionContext<X> );
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
template<capy::ExecutionContext Ctx>
void spawn_work( Ctx& ctx, capy::task<> work )
{
    auto ex = ctx.get_executor();
    capy::run_async(ex)(std::move(work)); // schedules work; runs on ctx
}
// end::example_2[]
} // namespace ex_2

} // namespace
