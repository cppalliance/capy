//
// Copyright (c) 2021 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_HELPERS_HPP
#define BOOST_CAPY_TEST_HELPERS_HPP

// Trick boostdep into requiring URL
// since we need it for the unit tests
#ifdef BOOST_CAPY_BOOSTDEP
#include <boost/url/url.hpp>
#endif

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/execution_context.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {

//----------------------------------------------------------
// Test Utilities
//----------------------------------------------------------

class test_context : public execution_context
{
public:
    int id = 0;
};

inline test_context&
default_test_context() noexcept
{
    static test_context ctx;
    return ctx;
}

/** Simple synchronous executor for testing.

    Satisfies the Executor concept.
    Executes inline (returns the handle for symmetric transfer).
    Uses pointers to external storage to allow copying.
*/
struct test_executor
{
    int id_ = 0;
    int* dispatch_count_ = nullptr;
    test_context* ctx_ = nullptr;

    test_executor() = default;

    explicit
    test_executor(test_context& ctx) noexcept
        : ctx_(&ctx)
    {
    }

    explicit
    test_executor(int& count) noexcept
        : dispatch_count_(&count)
    {
    }

    test_executor(int id, int& count) noexcept
        : id_(id)
        , dispatch_count_(&count)
    {
    }

    bool
    operator==(test_executor const& other) const noexcept
    {
        return id_ == other.id_ &&
               dispatch_count_ == other.dispatch_count_ &&
               ctx_ == other.ctx_;
    }

    execution_context&
    context() const noexcept
    {
        return ctx_ ? *ctx_ : default_test_context();
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    coro
    dispatch(coro h) const
    {
        if(dispatch_count_)
            ++(*dispatch_count_);
        return h;
    }

    void
    post(coro h) const
    {
        h.resume();
    }
};

static_assert(Executor<test_executor>);

} // capy
} // boost

#endif
