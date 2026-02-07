//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/detail/thread_local_ptr.hpp>

#include <thread>

#include "test_suite.hpp"

namespace boost {
namespace capy {

// Test class type
struct widget
{
    int value;
    explicit widget(int v) : value(v) {}
};

struct thread_local_ptr_test
{
    void
    run()
    {
        // default state is nullptr
        {
            detail::thread_local_ptr<int> p;
            BOOST_TEST(p.get() == nullptr);
        }

        // set and get with int*
        {
            detail::thread_local_ptr<int> p;
            int x = 42;
            p.set(&x);
            BOOST_TEST(p.get() == &x);
            BOOST_TEST(*p.get() == 42);

            // clear
            p.set(nullptr);
            BOOST_TEST(p.get() == nullptr);
        }

        // operator= for setting
        {
            detail::thread_local_ptr<int> p;
            int x = 100;
            int* result = (p = &x);
            BOOST_TEST(result == &x);
            BOOST_TEST(p.get() == &x);
        }

        // operator* dereference
        {
            detail::thread_local_ptr<int> p;
            int x = 55;
            p = &x;
            BOOST_TEST(*p == 55);

            *p = 66;
            BOOST_TEST(x == 66);
        }

        // operator-> for class types
        {
            detail::thread_local_ptr<widget> p;
            widget w(123);
            p = &w;
            BOOST_TEST(p->value == 123);
            p->value = 456;
            BOOST_TEST(w.value == 456);
        }

        // all instances of same type share slot
        {
            detail::thread_local_ptr<int> p1;
            detail::thread_local_ptr<int> p2;

            int a = 1;

            p1 = &a;

            // Same type = same slot
            BOOST_TEST(p1.get() == &a);
            BOOST_TEST(p2.get() == &a);
            BOOST_TEST(p1.get() == p2.get());
        }

        // thread independence
        {
            detail::thread_local_ptr<long> p;
            long main_val = 100;
            long thread_val = 200;

            p = &main_val;
            BOOST_TEST(p.get() == &main_val);

            bool thread_saw_nullptr = false;
            bool thread_set_worked = false;

            std::thread t([&]() {
                // New thread should see nullptr initially
                thread_saw_nullptr = (p.get() == nullptr);

                // Set in thread
                p = &thread_val;
                thread_set_worked = (p.get() == &thread_val);
            });
            t.join();

            BOOST_TEST(thread_saw_nullptr);
            BOOST_TEST(thread_set_worked);

            // Main thread should still see its value
            BOOST_TEST(p.get() == &main_val);
        }

        // multiple threads
        {
            detail::thread_local_ptr<widget> p;

            widget main_w(1);
            p = &main_w;

            std::thread t1([&]() {
                widget w(10);
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 12
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
                p = &w;
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 12
#pragma GCC diagnostic pop
#endif
                BOOST_TEST(p->value == 10);
            });

            std::thread t2([&]() {
                widget w(20);
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 12
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
                p = &w;
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 12
#pragma GCC diagnostic pop
#endif
                BOOST_TEST(p->value == 20);
            });

            t1.join();
            t2.join();

            // Main thread unchanged
            BOOST_TEST(p->value == 1);
        }
    }
};

TEST_SUITE(
    thread_local_ptr_test,
    "boost.capy.detail.thread_local_ptr");

} // capy
} // boost
