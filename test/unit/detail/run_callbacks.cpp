//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/detail/run_callbacks.hpp>

#include <exception>
#include <stdexcept>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace detail {

class run_callbacks_test
{
public:
    void
    test_default_handler()
    {
        default_handler h;
        h(42);  // value overload, discarded
        h();    // void overload

        std::exception_ptr null;
        h(null);  // null: no rethrow

        bool rethrown = false;
        std::exception_ptr ep =
            std::make_exception_ptr(std::runtime_error("x"));
        try
        {
            h(ep);
        }
        catch(std::runtime_error const&)
        {
            rethrown = true;
        }
        BOOST_TEST(rethrown);
    }

    void
    test_handler_pair()
    {
        int value = 0;
        bool voided = false;
        std::exception_ptr captured;

        auto vh = [&](auto&& v) noexcept { value = static_cast<int>(v); };
        auto eh = [&](std::exception_ptr ep) noexcept { captured = ep; };
        handler_pair<decltype(vh), decltype(eh)> hp{vh, eh};

        hp(7);
        BOOST_TEST(value == 7);

        auto vh2 = [&](auto&&...) noexcept { voided = true; };
        handler_pair<decltype(vh2), decltype(eh)> hp2{vh2, eh};
        hp2();
        BOOST_TEST(voided);

        auto ep = std::make_exception_ptr(std::runtime_error("e"));
        hp2(ep);
        BOOST_TEST(captured == ep);
    }

    // A value-only handler: invocable with int and with no args, but not
    // with exception_ptr, so handler_pair's if-constexpr takes the rethrow
    // branch. A generic lambda cannot be used here: the invocable trait
    // would hard-instantiate its body with exception_ptr.
    struct value_handler
    {
        int* value;
        int* calls;
        void operator()(int v) noexcept { ++*calls; *value = v; }
        void operator()() noexcept { ++*calls; }
    };

    void
    test_handler_pair_default()
    {
        // H1 not invocable with exception_ptr: the exception path rethrows.
        int value = 0;
        int calls = 0;
        handler_pair<value_handler, default_handler> hp{
            value_handler{&value, &calls}};

        hp(5);
        BOOST_TEST(value == 5);
        hp();  // void overload
        BOOST_TEST(calls == 2);

        bool rethrown = false;
        std::exception_ptr ep =
            std::make_exception_ptr(std::runtime_error("boom"));
        try
        {
            hp(ep);
        }
        catch(std::runtime_error const&)
        {
            rethrown = true;
        }
        BOOST_TEST(rethrown);
    }

    void
    test_handler_pair_default_invocable()
    {
        // H1 invocable with exception_ptr: the exception is forwarded to it.
        std::exception_ptr captured;
        auto h = [&](std::exception_ptr ep) noexcept { captured = ep; };
        handler_pair<decltype(h), default_handler> hp{h};

        auto ep = std::make_exception_ptr(std::runtime_error("fwd"));
        hp(ep);
        BOOST_TEST(captured == ep);
    }

    void
    run()
    {
        test_default_handler();
        test_handler_pair();
        test_handler_pair_default();
        test_handler_pair_default_invocable();
    }
};

TEST_SUITE(run_callbacks_test, "boost.capy.detail.run_callbacks");

} // detail
} // capy
} // boost
