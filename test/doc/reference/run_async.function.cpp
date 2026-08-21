//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::run_async, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/ex/run_async.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

// Examples leave results unused; the reference explains them in prose.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-function"
// gcc 15 with sanitizers misattributes coroutine frame delete paths
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
#if defined(_MSC_VER)
#pragma warning(disable: 4834) // discarding [[nodiscard]] return value
#pragma warning(disable: 4189) // local variable initialized but not referenced
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4101) // unreferenced local variable
#pragma warning(disable: 4456) // declaration hides previous local declaration
#pragma warning(disable: 4457) // declaration hides function parameter
#pragma warning(disable: 4458) // declaration hides class member
#pragma warning(disable: 4459) // declaration hides global declaration
#endif

#include <boost/capy.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace capy = boost::capy;
using namespace boost::capy;

namespace {
namespace ex_1 {
// tag::example_1[]
task<void> my_task() { co_return; }

void start_task( any_executor ex )
{
    run_async(ex)(my_task());
}
// end::example_1[]
} // namespace ex_1
namespace ex_2 {
// tag::example_2[]
task<int> compute_value() { co_return 42; }

template<class... Fs>
struct overloaded : Fs... { using Fs::operator()...; };
template<class... Fs>
overloaded(Fs...) -> overloaded<Fs...>;

// The handlers may run after this function returns, on whichever thread
// the executor schedules them, so the state they write must outlive
// the call.
int last_result = 0;
int last_result2 = 0;
bool last_failed = false;

void run_with_result_handler_demo( any_executor ex )
{
    run_async(ex, [](int result) {
        last_result = result;   // the successful value arrives here
    })(compute_value());

    // Overloaded handler for both result and exception
    overloaded handle_result_or_exception{
        [](int result) { last_result2 = result; },      // the successful value arrives here
        [](std::exception_ptr) { last_failed = true; }  // the failure arrives here
    };
    run_async(ex, handle_result_or_exception)(compute_value());
}
// end::example_2[]
} // namespace ex_2
namespace ex_3 {
// tag::example_3[]
task<int> compute_value() { co_return 42; }

// The handlers may run after this function returns, on whichever thread
// the executor schedules them, so the state they write must outlive
// the call.
int separate_handlers_result = 0;
std::string separate_handlers_error;

void run_with_separate_handlers_demo( any_executor ex )
{
    run_async(ex,
        [](int result) {
            separate_handlers_result = result;   // the successful value arrives here
        },
        [](std::exception_ptr ep) {
            try { std::rethrow_exception(ep); }
            catch (std::exception const& e) {
                separate_handlers_error = e.what();  // copied: the message outlives the exception
            }
        }
    )(compute_value());
}
// end::example_3[]
} // namespace ex_3
namespace ex_4 {
// tag::example_4[]
task<void> cancellable_task() { co_return; }

void run_with_cancellation( any_executor ex )
{
    std::stop_source source;
    run_async(ex, source.get_token())(cancellable_task());
    // Later: source.request_stop();
}
// end::example_4[]
} // namespace ex_4

} // namespace
