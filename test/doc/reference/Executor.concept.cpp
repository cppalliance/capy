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
class E
{
    capy::execution_context& ctx_;
    std::thread::id home_ = std::this_thread::get_id();

public:
    explicit E(capy::execution_context& ctx) noexcept : ctx_(ctx) {}

    capy::execution_context& context() const noexcept { return ctx_; }
    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    bool operator==(E const& other) const noexcept
    {
        return &ctx_ == &other.ctx_;
    }

    std::coroutine_handle<> dispatch(
        capy::continuation& c ) const
    {
        if( std::this_thread::get_id() == home_ )
            return c.h;            // symmetric transfer
        post( c );
        return std::noop_coroutine();
    }

    void post( capy::continuation& ) const
    {
        // enqueue for later execution on this executor's context
    }
};

static_assert( capy::Executor<E> );
// end::example_1[]
} // namespace ex_1

namespace ex_2 {
// tag::example_2[]
class E
{
public:
    capy::execution_context& context() const noexcept;

    void on_work_started() const noexcept;
    void on_work_finished() const noexcept;

    std::coroutine_handle<> dispatch(
        capy::continuation& c ) const;
    void post( capy::continuation& c ) const;

    bool operator==( E const& ) const noexcept;
};
// end::example_2[]
} // namespace ex_2

} // namespace
