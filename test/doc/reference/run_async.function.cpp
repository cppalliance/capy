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
capy::task<void> my_task() { co_return; }

void start_task( capy::any_executor ex )
{
    capy::run_async(ex)(my_task());
}
// end::example_1[]
} // namespace ex_1
namespace ex_2 {
// tag::example_2[]
capy::task<int> compute_value() { co_return 42; }

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

void run_with_result_handler_demo( capy::any_executor ex )
{
    capy::run_async(ex, [](int result) {
        last_result = result;   // the successful value arrives here
    })(compute_value());

    // Overloaded handler for both result and exception
    overloaded handle_result_or_exception{
        [](int result) { last_result2 = result; },      // the successful value arrives here
        [](std::exception_ptr) { last_failed = true; }  // the failure arrives here
    };
    capy::run_async(ex, handle_result_or_exception)(compute_value());
}
// end::example_2[]
} // namespace ex_2
namespace ex_3 {
// tag::example_3[]
capy::task<int> compute_value() { co_return 42; }

// The handlers may run after this function returns, on whichever thread
// the executor schedules them, so the state they write must outlive
// the call.
int separate_handlers_result = 0;
std::string separate_handlers_error;

void run_with_separate_handlers_demo( capy::any_executor ex )
{
    capy::run_async(ex,
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
capy::task<void> cancellable_task() { co_return; }

void run_with_cancellation( capy::any_executor ex )
{
    std::stop_source source;
    capy::run_async(ex, source.get_token())(cancellable_task());
    // Later: source.request_stop();
}
// end::example_4[]
} // namespace ex_4

} // namespace
