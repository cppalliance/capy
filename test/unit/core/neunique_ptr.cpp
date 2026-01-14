//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/core/neunique_ptr.hpp>

#include "test_suite.hpp"

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <unordered_map>

namespace boost {
namespace capy {

//----------------------------------------------------------
// Test types
//----------------------------------------------------------

static int destroyed = 0;

struct Base
{
    virtual ~Base() = default;
    virtual int value() const = 0;
};

struct Derived : Base
{
    int x;
    explicit Derived(int v) : x(v) {}
    int value() const override { return x; }
};

struct Tracked
{
    int x;
    explicit Tracked(int v) : x(v) {}
    ~Tracked() { ++destroyed; }
};

struct ThrowOnCopy
{
    ThrowOnCopy() = default;
    ThrowOnCopy(ThrowOnCopy const&) { throw std::runtime_error("copy"); }
    ThrowOnCopy(ThrowOnCopy&&) = default;
};

struct CustomDeleter
{
    int* call_count;

    explicit CustomDeleter(int* p) : call_count(p) {}

    void operator()(int* p) const
    {
        if(call_count)
            ++(*call_count);
        delete p;
    }
};

struct CustomArrayDeleter
{
    int* call_count;

    explicit CustomArrayDeleter(int* p) : call_count(p) {}

    void operator()(int* p) const
    {
        if(call_count)
            ++(*call_count);
        delete[] p;
    }
};

template<class T>
struct TrackingAllocator
{
    using value_type = T;

    int* alloc_count;
    int* dealloc_count;

    TrackingAllocator(int* ac, int* dc)
        : alloc_count(ac)
        , dealloc_count(dc)
    {
    }

    template<class U>
    TrackingAllocator(TrackingAllocator<U> const& other)
        : alloc_count(other.alloc_count)
        , dealloc_count(other.dealloc_count)
    {
    }

    T* allocate(std::size_t n)
    {
        if(alloc_count)
            ++(*alloc_count);
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t)
    {
        if(dealloc_count)
            ++(*dealloc_count);
        ::operator delete(p);
    }

    template<class U>
    bool operator==(TrackingAllocator<U> const& other) const
    {
        return alloc_count == other.alloc_count &&
               dealloc_count == other.dealloc_count;
    }

    template<class U>
    bool operator!=(TrackingAllocator<U> const& other) const
    {
        return !(*this == other);
    }
};

struct EmptyDeleter
{
    void operator()(int* p) const { delete p; }
};

//----------------------------------------------------------

struct neunique_ptr_test
{
    void
    test_default_construction()
    {
        neunique_ptr<int> p1;
        neunique_ptr<int> p2(nullptr);

        BOOST_TEST(!p1);
        BOOST_TEST(!p2);
        BOOST_TEST(p1.get() == nullptr);
        BOOST_TEST(p2.get() == nullptr);
    }

    void
    test_raw_pointer_construction()
    {
        neunique_ptr<int> p1(new int(42));
        BOOST_TEST(p1);
        BOOST_TEST(*p1 == 42);
        BOOST_TEST(p1.get() != nullptr);

        neunique_ptr<int> p2(nullptr);
        BOOST_TEST(!p2);
    }

    void
    test_custom_deleter()
    {
        int call_count = 0;
        {
            neunique_ptr<int> p(new int(10), CustomDeleter(&call_count));
            BOOST_TEST(p);
            BOOST_TEST(*p == 10);
        }
        BOOST_TEST(call_count == 1);
    }

    void
    test_custom_deleter_and_allocator()
    {
        int delete_count = 0;
        int alloc_count = 0;
        int dealloc_count = 0;

        {
            TrackingAllocator<int> alloc(&alloc_count, &dealloc_count);
            neunique_ptr<int> p(
                new int(20),
                CustomDeleter(&delete_count),
                alloc);
            BOOST_TEST(p);
            BOOST_TEST(*p == 20);
            BOOST_TEST(alloc_count > 0);
        }
        BOOST_TEST(delete_count == 1);
        BOOST_TEST(dealloc_count > 0);
    }

    void
    test_make_neunique()
    {
        auto p1 = make_neunique<int>(42);
        BOOST_TEST(p1);
        BOOST_TEST(*p1 == 42);

        auto p2 = make_neunique<Derived>(99);
        BOOST_TEST(p2);
        BOOST_TEST(p2->value() == 99);
    }

    void
    test_allocate_neunique()
    {
        int alloc_count = 0;
        int dealloc_count = 0;
        TrackingAllocator<int> alloc(&alloc_count, &dealloc_count);

        {
            auto p = allocate_neunique<int>(alloc, 55);
            BOOST_TEST(p);
            BOOST_TEST(*p == 55);
            BOOST_TEST(alloc_count > 0);
        }
        BOOST_TEST(dealloc_count > 0);
    }

    void
    test_move_construction()
    {
        neunique_ptr<int> p1(new int(42));
        int* raw = p1.get();

        neunique_ptr<int> p2(std::move(p1));
        BOOST_TEST(!p1);
        BOOST_TEST(p2);
        BOOST_TEST(p2.get() == raw);
        BOOST_TEST(*p2 == 42);
    }

    void
    test_converting_move_construction()
    {
        neunique_ptr<Derived> p1(new Derived(42));
        neunique_ptr<Base> p2(std::move(p1));

        BOOST_TEST(!p1);
        BOOST_TEST(p2);
        BOOST_TEST(p2->value() == 42);
    }

    void
    test_aliasing_constructor()
    {
        struct Pair
        {
            int first;
            int second;
            Pair(int a, int b) : first(a), second(b) {}
        };
        auto p1 = allocate_neunique<Pair>(std::allocator<Pair>{}, 10, 20);
        int* raw_second = &p1->second;
        neunique_ptr<int> p2(std::move(p1), raw_second);
        BOOST_TEST(!p1);
        BOOST_TEST(p2);
        BOOST_TEST(p2.get() == raw_second);
        BOOST_TEST(*p2 == 20);
    }

    void
    test_move_assignment()
    {
        neunique_ptr<int> p1(new int(42));
        neunique_ptr<int> p2(new int(99));
        int* raw = p1.get();

        p2 = std::move(p1);
        BOOST_TEST(!p1);
        BOOST_TEST(p2);
        BOOST_TEST(p2.get() == raw);
        BOOST_TEST(*p2 == 42);
    }

    void
    test_converting_move_assignment()
    {
        neunique_ptr<Derived> p1(new Derived(42));
        neunique_ptr<Base> p2;

        p2 = std::move(p1);
        BOOST_TEST(!p1);
        BOOST_TEST(p2);
        BOOST_TEST(p2->value() == 42);
    }

    void
    test_nullptr_assignment()
    {
        neunique_ptr<int> p(new int(42));
        BOOST_TEST(p);

        p = nullptr;
        BOOST_TEST(!p);
        BOOST_TEST(p.get() == nullptr);
    }

    void
    test_reset()
    {
        destroyed = 0;
        {
            neunique_ptr<Tracked> p(new Tracked(1));
            BOOST_TEST(destroyed == 0);

            p.reset();
            BOOST_TEST(destroyed == 1);
            BOOST_TEST(!p);

            p.reset(new Tracked(2));
            BOOST_TEST(p);
            BOOST_TEST(p->x == 2);
        }
        BOOST_TEST(destroyed == 2);
    }

    void
    test_reset_with_deleter()
    {
        int delete_count = 0;
        neunique_ptr<int> p(new int(10));

        p.reset(new int(20), CustomDeleter(&delete_count));
        BOOST_TEST(p);
        BOOST_TEST(*p == 20);
        BOOST_TEST(delete_count == 0);

        p.reset();
        BOOST_TEST(delete_count == 1);
    }

    void
    test_reset_with_deleter_and_allocator()
    {
        int delete_count = 0;
        int alloc_count = 0;
        int dealloc_count = 0;
        TrackingAllocator<int> alloc(&alloc_count, &dealloc_count);

        neunique_ptr<int> p(new int(10));
        p.reset(new int(30), CustomDeleter(&delete_count), alloc);

        BOOST_TEST(p);
        BOOST_TEST(*p == 30);
        BOOST_TEST(alloc_count > 0);

        p.reset();
        BOOST_TEST(delete_count == 1);
        BOOST_TEST(dealloc_count > 0);
    }

    void
    test_swap()
    {
        neunique_ptr<int> p1(new int(42));
        neunique_ptr<int> p2(new int(99));
        int* raw1 = p1.get();
        int* raw2 = p2.get();

        p1.swap(p2);
        BOOST_TEST(p1.get() == raw2);
        BOOST_TEST(p2.get() == raw1);
        BOOST_TEST(*p1 == 99);
        BOOST_TEST(*p2 == 42);

        swap(p1, p2);
        BOOST_TEST(p1.get() == raw1);
        BOOST_TEST(p2.get() == raw2);
    }

    void
    test_observers()
    {
        neunique_ptr<int> p1(new int(42));
        BOOST_TEST(p1.get() != nullptr);
        BOOST_TEST(static_cast<bool>(p1));
        BOOST_TEST(*p1 == 42);
        BOOST_TEST(p1.operator->() == p1.get());

        neunique_ptr<int> p2;
        BOOST_TEST(p2.get() == nullptr);
        BOOST_TEST(!static_cast<bool>(p2));
    }

    void
    test_destruction()
    {
        destroyed = 0;
        {
            neunique_ptr<Tracked> p(new Tracked(1));
        }
        BOOST_TEST(destroyed == 1);

        destroyed = 0;
        {
            int delete_count = 0;
            neunique_ptr<Tracked> p(
                new Tracked(2),
                [&](Tracked* t)
                {
                    ++delete_count;
                    delete t;
                });
        }
        BOOST_TEST(destroyed == 1);
    }

    void
    test_self_move_assignment()
    {
        neunique_ptr<int> p(new int(42));
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 13)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
        p = std::move(p);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
        BOOST_TEST(p);
        BOOST_TEST(*p == 42);
    }

    void
    test_comparison_operators()
    {
        neunique_ptr<int> p1(new int(42));
        neunique_ptr<int> p2(new int(99));
        neunique_ptr<int> p3;

        BOOST_TEST(p1 == p1);
        BOOST_TEST(p1 != p2);
        BOOST_TEST(p3 == nullptr);
        BOOST_TEST(nullptr == p3);
        BOOST_TEST(p1 != nullptr);
        BOOST_TEST(nullptr != p1);

        BOOST_TEST((p1 < p2) == (p1.get() < p2.get()));
        BOOST_TEST((p1 <= p2) == (p1.get() <= p2.get()));
        BOOST_TEST((p1 > p2) == (p1.get() > p2.get()));
        BOOST_TEST((p1 >= p2) == (p1.get() >= p2.get()));
    }

    void
    test_hash()
    {
        neunique_ptr<int> p1(new int(42));
        neunique_ptr<int> p2(new int(99));

        std::hash<neunique_ptr<int>> hasher;
        std::size_t h1 = hasher(p1);
        std::size_t h2 = hasher(p2);

        BOOST_TEST(h1 == std::hash<int*>()(p1.get()));
        BOOST_TEST(h2 == std::hash<int*>()(p2.get()));

        std::unordered_map<neunique_ptr<int>, int> map;
        map[std::move(p1)] = 42;
        BOOST_TEST(map.size() == 1);
    }

    void
    test_array_default_construction()
    {
        neunique_ptr<int[]> p1;
        neunique_ptr<int[]> p2(nullptr);

        BOOST_TEST(!p1);
        BOOST_TEST(!p2);
        BOOST_TEST(p1.get() == nullptr);
        BOOST_TEST(p2.get() == nullptr);
    }

    void
    test_array_raw_pointer_construction()
    {
        neunique_ptr<int[]> p(new int[3]{1, 2, 3});
        BOOST_TEST(p);
        BOOST_TEST(p[0] == 1);
        BOOST_TEST(p[1] == 2);
        BOOST_TEST(p[2] == 3);
    }

    void
    test_array_custom_deleter()
    {
        int call_count = 0;
        {
            neunique_ptr<int[]> p(
                new int[3]{1, 2, 3},
                CustomArrayDeleter(&call_count));
            BOOST_TEST(p);
            BOOST_TEST(p[0] == 1);
        }
        BOOST_TEST(call_count == 1);
    }

    void
    test_array_make_neunique()
    {
        auto p = make_neunique<int[]>(5);
        BOOST_TEST(p);
        for(int i = 0; i < 5; ++i)
            BOOST_TEST(p[i] == 0);
    }

    void
    test_array_allocate_neunique()
    {
        int alloc_count = 0;
        int dealloc_count = 0;
        TrackingAllocator<int> alloc(&alloc_count, &dealloc_count);

        {
            auto p = allocate_neunique<int[]>(alloc, 3);
            BOOST_TEST(p);
            BOOST_TEST(alloc_count > 0);
            p[0] = 10;
            p[1] = 20;
            p[2] = 30;
            BOOST_TEST(p[0] == 10);
            BOOST_TEST(p[1] == 20);
            BOOST_TEST(p[2] == 30);
        }
        BOOST_TEST(dealloc_count > 0);
    }

    void
    test_array_move_construction()
    {
        neunique_ptr<int[]> p1(new int[3]{1, 2, 3});
        int* raw = p1.get();

        neunique_ptr<int[]> p2(std::move(p1));
        BOOST_TEST(!p1);
        BOOST_TEST(p2);
        BOOST_TEST(p2.get() == raw);
        BOOST_TEST(p2[0] == 1);
    }

    void
    test_array_move_assignment()
    {
        neunique_ptr<int[]> p1(new int[3]{1, 2, 3});
        neunique_ptr<int[]> p2(new int[2]{4, 5});
        int* raw = p1.get();

        p2 = std::move(p1);
        BOOST_TEST(!p1);
        BOOST_TEST(p2);
        BOOST_TEST(p2.get() == raw);
        BOOST_TEST(p2[0] == 1);
    }

    void
    test_array_reset()
    {
        neunique_ptr<int[]> p(new int[3]{1, 2, 3});
        BOOST_TEST(p);

        p.reset();
        BOOST_TEST(!p);

        p.reset(new int[2]{4, 5});
        BOOST_TEST(p);
        BOOST_TEST(p[0] == 4);
        BOOST_TEST(p[1] == 5);
    }

    void
    test_array_swap()
    {
        neunique_ptr<int[]> p1(new int[2]{1, 2});
        neunique_ptr<int[]> p2(new int[2]{3, 4});
        int* raw1 = p1.get();
        int* raw2 = p2.get();

        p1.swap(p2);
        BOOST_TEST(p1.get() == raw2);
        BOOST_TEST(p2.get() == raw1);
        BOOST_TEST(p1[0] == 3);
        BOOST_TEST(p2[0] == 1);
    }

    void
    test_array_observers()
    {
        neunique_ptr<int[]> p1(new int[3]{1, 2, 3});
        BOOST_TEST(p1.get() != nullptr);
        BOOST_TEST(static_cast<bool>(p1));
        BOOST_TEST(p1[0] == 1);
        BOOST_TEST(p1[1] == 2);
        BOOST_TEST(p1[2] == 3);

        neunique_ptr<int[]> p2;
        BOOST_TEST(p2.get() == nullptr);
        BOOST_TEST(!static_cast<bool>(p2));
    }

    void
    test_ebo()
    {
        // Test that empty base optimization is working
        // for empty deleters in the control block
        neunique_ptr<int> p(new int(42), EmptyDeleter());
        BOOST_TEST(p);
        BOOST_TEST(*p == 42);
    }

    void
    test_incomplete_type()
    {
        // Test that neunique_ptr can be declared with
        // incomplete types (implementation detail test)
        struct Incomplete;
        neunique_ptr<Incomplete> p1;
        neunique_ptr<Incomplete> p2(nullptr);
        BOOST_TEST(!p1);
        BOOST_TEST(!p2);
    }

    void
    run()
    {
        test_default_construction();
        test_raw_pointer_construction();
        test_custom_deleter();
        test_custom_deleter_and_allocator();
        test_make_neunique();
        test_allocate_neunique();
        test_move_construction();
        test_converting_move_construction();
        test_aliasing_constructor();
        test_move_assignment();
        test_converting_move_assignment();
        test_nullptr_assignment();
        test_reset();
        test_reset_with_deleter();
        test_reset_with_deleter_and_allocator();
        test_swap();
        test_observers();
        test_destruction();
        test_self_move_assignment();
        test_comparison_operators();
        test_hash();

        test_array_default_construction();
        test_array_raw_pointer_construction();
        test_array_custom_deleter();
        test_array_make_neunique();
        test_array_allocate_neunique();
        test_array_move_construction();
        test_array_move_assignment();
        test_array_reset();
        test_array_swap();
        test_array_observers();

        test_ebo();
        test_incomplete_type();
    }
};

TEST_SUITE(
    neunique_ptr_test,
    "boost.capy.neunique_ptr");

} // capy
} // boost
