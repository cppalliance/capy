//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/test/thread_name.hpp>

#include "test_helpers.hpp"

namespace boost {
namespace capy {

struct thread_name_test
{
    void
    testSetName()
    {
#if defined(BOOST_CAPY_TEST_CAN_GET_THREAD_NAME)
        set_current_thread_name("test-thread");
        BOOST_TEST(check_thread_name("test-thread"));

        set_current_thread_name("capy-pool-0");
        BOOST_TEST(check_thread_name("capy-pool-0"));

        // Long name is truncated to 15 chars on Linux/FreeBSD/NetBSD
        set_current_thread_name(
            "this-is-a-very-long-thread-name-that-exceeds-limits");
#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__)
        BOOST_TEST(check_thread_name("this-is-a-very-"));
#elif defined(__APPLE__)
        // macOS truncates to 63 chars
        BOOST_TEST(check_thread_name(
            "this-is-a-very-long-thread-name-that-exceeds-limits"));
#elif defined(_WIN32)
        // Windows has no practical limit
        BOOST_TEST(check_thread_name(
            "this-is-a-very-long-thread-name-that-exceeds-limits"));
#endif

        // Test macOS 63-char limit specifically
        set_current_thread_name(
            "0123456789012345678901234567890123456789012345678901234567890123456789");
#if defined(__APPLE__)
        // Truncated to 63 chars
        BOOST_TEST(check_thread_name(
            "012345678901234567890123456789012345678901234567890123456789012"));
#endif

#if defined(_WIN32)
        // Windows UTF-8 support (simple ASCII subset)
        set_current_thread_name("worker-thread-1");
        BOOST_TEST(check_thread_name("worker-thread-1"));
#endif
#endif // BOOST_CAPY_TEST_CAN_GET_THREAD_NAME

        // Empty string should not crash (but we don't verify the result
        // since some platforms may not support clearing thread names)
        set_current_thread_name("");
    }

    void
    run()
    {
        testSetName();
    }
};

TEST_SUITE(
    thread_name_test,
    "boost.capy.thread_name");

} // capy
} // boost
