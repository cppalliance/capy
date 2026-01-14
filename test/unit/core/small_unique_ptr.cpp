//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/core/small_unique_ptr.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>
#include <new>

#include "test_suite.hpp"

struct Base
{
    virtual ~Base() = default;
    virtual int value() const = 0;
};

struct Small : Base
{
    int x;
    Small(int v) : x(v) {}
    int value() const override { return x; }
};

struct Large : Base
{
    int data[64];
    Large(int v) { data[0] = v; }
    int value() const override { return data[0]; }
};

static int destroyed = 0;

struct Tracked : Base
{
    int x;
    Tracked(int v) : x(v) {}
    ~Tracked() { ++destroyed; }
    int value() const override { return x; }
};

struct TrackedLarge : Base
{
    int data[64];
    TrackedLarge(int v) { data[0] = v; }
    ~TrackedLarge() { ++destroyed; }
    int value() const override { return data[0]; }
};

namespace boost {
namespace capy {

struct small_unique_ptr_test
{
    void
    run()
    {
        // default and nullptr construction
        small_unique_ptr<Base, 64> p1;
        small_unique_ptr<Base, 64> p2(nullptr);
        BOOST_TEST(!p1);
        BOOST_TEST(!p2);
        BOOST_TEST(p1.get() == nullptr);

        // SBO construction (Small fits in buffer)
        auto p3 = make_small_unique<Base, 64, Small>(42);
        BOOST_TEST(p3);
        BOOST_TEST(p3->value() == 42);
        BOOST_TEST((*p3).value() == 42);

        // heap construction via make_small_unique (Large doesn't fit)
        auto p3b = make_small_unique<Base, 64, Large>(88);
        BOOST_TEST(p3b);
        BOOST_TEST(p3b->value() == 88);

        // heap construction via raw pointer (Large doesn't fit)
        small_unique_ptr<Base, 64> p4(new Large(99));
        BOOST_TEST(p4);
        BOOST_TEST(p4->value() == 99);

        // move construction (SBO)
        auto p5 = std::move(p3);
        BOOST_TEST(p5);
        BOOST_TEST(!p3);
        BOOST_TEST(p5->value() == 42);

        // move construction (heap)
        auto p5b = std::move(p3b);
        BOOST_TEST(p5b);
        BOOST_TEST(!p3b);
        BOOST_TEST(p5b->value() == 88);

        // move assignment (SBO)
        p3 = std::move(p5);
        BOOST_TEST(p3);
        BOOST_TEST(!p5);
        BOOST_TEST(p3->value() == 42);

        // move assignment (heap)
        p3b = std::move(p5b);
        BOOST_TEST(p3b);
        BOOST_TEST(!p5b);
        BOOST_TEST(p3b->value() == 88);

        // nullptr assignment
        p3 = nullptr;
        BOOST_TEST(!p3);

        // reset and destruction tracking (SBO)
        destroyed = 0;
        {
            auto p = make_small_unique<Base, 64, Tracked>(1);
            BOOST_TEST(destroyed == 0);
            p.reset();
            BOOST_TEST(destroyed == 1);
            BOOST_TEST(!p);
        }
        BOOST_TEST(destroyed == 1);

        // destructor (SBO)
        destroyed = 0;
        {
            auto p = make_small_unique<Base, 64, Tracked>(2);
        }
        BOOST_TEST(destroyed == 1);

        // destructor (heap via raw pointer)
        destroyed = 0;
        {
            small_unique_ptr<Base, 64> p(new Tracked(3));
        }
        BOOST_TEST(destroyed == 1);

        // destructor (heap via make_small_unique)
        destroyed = 0;
        {
            auto p = make_small_unique<Base, 64, TrackedLarge>(4);
        }
        BOOST_TEST(destroyed == 1);

        // swap (SBO)
        auto pa = make_small_unique<Base, 64, Small>(10);
        auto pb = make_small_unique<Base, 64, Small>(20);
        swap(pa, pb);
        BOOST_TEST(pa->value() == 20);
        BOOST_TEST(pb->value() == 10);

        // swap (heap)
        auto pc = make_small_unique<Base, 64, Large>(30);
        auto pd = make_small_unique<Base, 64, Large>(40);
        swap(pc, pd);
        BOOST_TEST(pc->value() == 40);
        BOOST_TEST(pd->value() == 30);

        // self-move-assignment (should be safe)
        auto p6 = make_small_unique<Base, 64, Small>(77);
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 13)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
        p6 = std::move(p6);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
        BOOST_TEST(p6);
        BOOST_TEST(p6->value() == 77);

        // release (heap)
        small_unique_ptr<Base, 64> p7(new Large(55));
        Base* raw = p7.release();
        BOOST_TEST(!p7);
        BOOST_TEST(raw->value() == 55);
        delete raw;

        // move from empty
        small_unique_ptr<Base, 64> empty;
        auto p8 = std::move(empty);
        BOOST_TEST(!p8);
        BOOST_TEST(!empty);
    }
};

TEST_SUITE(
    small_unique_ptr_test,
    "boost.capy.small_unique_ptr");

} // capy
} // boost