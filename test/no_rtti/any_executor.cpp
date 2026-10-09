//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/any_executor.hpp>

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include "test_suite.hpp"

/* This file is built with RTTI disabled (-fno-rtti, /GR-). It proves
   that the type-erased executors identify types through detail::type_id
   and not through typeid. The #error below fails the build if the flag
   did not take effect, so a passing run cannot mean RTTI was on.

   Only executor types local to this file are type-erased here. Under
   CMake the capy library is built once, with RTTI on, and detail::type_id
   has a different definition in each mode. Instantiating any_executor or
   executor_ref for a library type such as thread_pool::executor_type in
   this TU would therefore be an ODR violation against the library.
*/

#if !BOOST_CAPY_NO_RTTI
# error "test/no_rtti must be compiled with RTTI disabled"
#endif

namespace boost {
namespace capy {

namespace {

// N only makes distinct types. ctx is never dereferenced: context()
// is instantiated by the type erasure but never called, so no
// execution_context object (which lives in the library) is needed.
template<int N>
struct local_executor
{
    int id = 0;
    execution_context* ctx = nullptr;

    bool operator==(local_executor const& other) const noexcept
    {
        return id == other.id;
    }
    execution_context& context() const noexcept { return *ctx; }
    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}
    std::coroutine_handle<> dispatch(continuation& c) const { return c.h; }
    void post(continuation&) const {}
};

using exec_a = local_executor<0>;
using exec_b = local_executor<1>;

static_assert(Executor<exec_a>);
static_assert(Executor<any_executor>);

} // namespace

struct any_executor_no_rtti_test
{
    void
    testTargetType()
    {
        any_executor empty;
        BOOST_TEST(empty.target_type() == detail::type_id<void>());

        any_executor a(exec_a{});
        BOOST_TEST(a.target_type() == detail::type_id<exec_a>());
        BOOST_TEST(a.target_type() != detail::type_id<exec_b>());
    }

    void
    testEquality()
    {
        any_executor a1(exec_a{1});
        any_executor a1_again(exec_a{1});
        any_executor a2(exec_a{2});
        any_executor b1(exec_b{1});

        BOOST_TEST(a1 == a1_again);
        BOOST_TEST(!(a1 == a2));
        // Equal ids, different types: equals() must reject on type
        // before comparing values.
        BOOST_TEST(!(a1 == b1));
    }

    void
    testTarget()
    {
        any_executor empty;
        BOOST_TEST(empty.target<exec_a>() == nullptr);

        any_executor a(exec_a{7});
        any_executor const& ca = a;
        BOOST_TEST(a.target<exec_a>() != nullptr);
        BOOST_TEST_EQ(a.target<exec_a>()->id, 7);
        BOOST_TEST_EQ(ca.target<exec_a>()->id, 7);
        BOOST_TEST(a.target<exec_a const>() == a.target<exec_a>());
        BOOST_TEST(a.target<exec_b>() == nullptr);
        BOOST_TEST(ca.target<exec_b>() == nullptr);
    }

    void
    testExecutorRef()
    {
        exec_a ea{3};
        executor_ref r(ea);
        BOOST_TEST(r.target<exec_a>() == &ea);
        BOOST_TEST(r.target<exec_b>() == nullptr);
    }

    void
    run()
    {
        testTargetType();
        testEquality();
        testTarget();
        testExecutorRef();
    }
};

TEST_SUITE(
    any_executor_no_rtti_test,
    "boost.capy.no_rtti.any_executor");

} // capy
} // boost
