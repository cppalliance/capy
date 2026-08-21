//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::Executor, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/concept/executor.hpp
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
class E
{
    execution_context& ctx_;
    std::thread::id home_ = std::this_thread::get_id();

public:
    explicit E(execution_context& ctx) noexcept : ctx_(ctx) {}

    execution_context& context() const noexcept { return ctx_; }
    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    bool operator==(E const& other) const noexcept
    {
        return &ctx_ == &other.ctx_;
    }

    std::coroutine_handle<> dispatch(
        continuation& c ) const
    {
        if( std::this_thread::get_id() == home_ )
            return c.h;            // symmetric transfer
        post( c );
        return std::noop_coroutine();
    }

    void post( continuation& ) const
    {
        // enqueue for later execution on this executor's context
    }
};

static_assert( Executor<E> );
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
class E
{
public:
    execution_context& context() const noexcept;

    void on_work_started() const noexcept;
    void on_work_finished() const noexcept;

    std::coroutine_handle<> dispatch(
        continuation& c ) const;
    void post( continuation& c ) const;

    bool operator==( E const& ) const noexcept;
};
// end::example_2[]
} // namespace ex_2

} // namespace
