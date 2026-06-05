//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/detail/await_suspend_helper.hpp>

#include <boost/capy/ex/io_env.hpp>

#include <coroutine>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace detail {

class await_suspend_helper_test
{
    // await_suspend returning void: caller suspends unconditionally.
    struct void_awaitable
    {
        bool suspended = false;
        void await_suspend(std::coroutine_handle<>, io_env const*)
        {
            suspended = true;
        }
    };

    // await_suspend returning bool: true suspends, false resumes.
    struct bool_awaitable
    {
        bool value;
        bool await_suspend(std::coroutine_handle<>, io_env const*)
        {
            return value;
        }
    };

    // await_suspend returning a handle: symmetric transfer to it.
    struct handle_awaitable
    {
        std::coroutine_handle<> next;
        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<>, io_env const*)
        {
            return next;
        }
    };

public:
    void
    run()
    {
        auto const h = std::noop_coroutine();

        // void -> noop_coroutine, and the awaitable was invoked.
        void_awaitable va;
        BOOST_TEST(call_await_suspend(&va, h, nullptr) == h);
        BOOST_TEST(va.suspended);

        // bool true -> noop_coroutine (stay suspended).
        bool_awaitable bt{true};
        BOOST_TEST(call_await_suspend(&bt, h, nullptr) == h);

        // bool false -> the original handle (resume).
        bool_awaitable bf{false};
        BOOST_TEST(call_await_suspend(&bf, h, nullptr) == h);

        // handle -> the returned handle.
        handle_awaitable ha{h};
        BOOST_TEST(call_await_suspend(&ha, h, nullptr) == h);
    }
};

TEST_SUITE(
    await_suspend_helper_test,
    "boost.capy.detail.await_suspend_helper");

} // detail
} // capy
} // boost
