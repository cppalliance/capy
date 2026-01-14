//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/core/polystore.hpp>
#include <boost/core/detail/static_assert.hpp>

#include "test_suite.hpp"

namespace boost {
namespace capy {

struct polystore_test
{
    void testFind()
    {
        struct T { int i = 1; };
        polystore ps;
        BOOST_TEST(ps.find<T>() == nullptr);
        ps.use<T>();
        if(! BOOST_TEST_NE(ps.find<T>(), nullptr))
            return;
        BOOST_TEST_EQ(ps.find<T>()->i, 1);
    }

    void testGet()
    {
        struct T { int i = 1; };
        polystore ps;
        BOOST_TEST_THROWS(ps.get<T>(), std::bad_typeid);
        ps.use<T>();
        BOOST_TEST_NO_THROW(ps.get<T>());
        BOOST_TEST_EQ(ps.get<T>().i, 1);
    }

    void testEmplaceAnon()
    {
        struct T { int i = 1; };
        polystore ps;
        auto& t = ps.emplace_anon<T>();
        BOOST_TEST_EQ(t.i, 1);
        t.i = 2;
        BOOST_TEST_EQ(t.i, 2);
        BOOST_TEST(ps.find<T>() == nullptr);
        BOOST_TEST_EQ(ps.emplace_anon<T>().i, 1);
    }

    void testInsertAnon()
    {
        struct T
        {
            int i = 1;

            explicit T(int i_) noexcept
                : i(i_)
            {
            }
        };
        polystore ps;
        auto& t = ps.insert_anon<T>(T{2});
        BOOST_TEST_EQ(t.i, 2);
        t.i = 3;
        BOOST_TEST_EQ(t.i, 3);
        BOOST_TEST(ps.find<T>() == nullptr);
        BOOST_TEST_EQ(ps.insert_anon<T>(T{4}).i, 4);
    }

    void testEmplace()
    {
        // with key_type
        {
            struct T
            {
                int t = 1;
            };

            struct U : T
            {
                using key_type = T;

                int u = 2;
            };

            polystore ps;
            auto& u = ps.emplace<U>();
            BOOST_TEST(ps.find<T>() != nullptr);
            BOOST_TEST(ps.find<U>() != nullptr);
            BOOST_TEST_EQ(u.t, 1);
            BOOST_TEST_EQ(u.u, 2);
            BOOST_TEST_EQ(ps.get<T>().t, 1);
            BOOST_TEST_EQ(ps.get<U>().u, 2);
            BOOST_TEST_THROWS(ps.emplace<U>(),
                std::invalid_argument);
            BOOST_TEST_THROWS(ps.emplace<T>(),
                std::invalid_argument);
        }

        // with Keys...
        {
            struct v1_t { int v = 1; };
            struct v2_t { int v = 2; };

            struct api
            {
                v1_t v1;
                v2_t v2;
                operator v1_t&() { return v1; }
                operator v2_t&() { return v2; }
            };

            polystore ps;
            auto& u = ps.emplace<api, v1_t, v2_t>();
            BOOST_TEST(ps.find<api>() != nullptr);
            BOOST_TEST(ps.find<v1_t>() != nullptr);
            BOOST_TEST(ps.find<v2_t>() != nullptr);
            BOOST_TEST_EQ(u.v1.v, 1);
            BOOST_TEST_EQ(u.v2.v, 2);
            BOOST_TEST_EQ(ps.get<v1_t>().v, 1);
            BOOST_TEST_EQ(ps.get<v2_t>().v, 2);
            BOOST_TEST_EQ(ps.get<api>().v1.v, 1);
            BOOST_TEST_EQ(ps.get<api>().v2.v, 2);
            BOOST_TEST_THROWS(ps.emplace<api>(),
                std::invalid_argument);
        }
    }

    void testTryEmplace()
    {
        // with key_type
        {
            struct T
            {
                int t = 1;
            };

            struct U : T
            {
                using key_type = T;

                int u = 2;
            };

            polystore ps;
            {
                auto& u = ps.try_emplace<U>();
                BOOST_TEST(ps.find<T>() != nullptr);
                BOOST_TEST(ps.find<U>() != nullptr);
                BOOST_TEST_EQ(u.t, 1);
                BOOST_TEST_EQ(u.u, 2);
                BOOST_TEST_EQ(ps.get<T>().t, 1);
                BOOST_TEST_EQ(ps.get<U>().u, 2);
                u.t = 8;
                u.u = 9;
                BOOST_TEST_EQ(ps.get<T>().t, 8);
                BOOST_TEST_EQ(ps.get<U>().u, 9);
            }
            {
                auto& u = ps.try_emplace<U>();
                BOOST_TEST_EQ(u.t, 8);
                BOOST_TEST_EQ(u.u, 9);
                BOOST_TEST_EQ(ps.get<T>().t, 8);
                BOOST_TEST_EQ(ps.get<U>().u, 9);
            }
        }

        // with Keys...
        {
            struct v1_t { int v = 1; };
            struct v2_t { int v = 2; };

            struct api
            {
                v1_t v1;
                v2_t v2;
                operator v1_t&() { return v1; }
                operator v2_t&() { return v2; }
            };

            polystore ps;
            {
                auto& u = ps.try_emplace<api, v1_t, v2_t>();
                BOOST_TEST(ps.find<api>() != nullptr);
                BOOST_TEST(ps.find<v1_t>() != nullptr);
                BOOST_TEST(ps.find<v2_t>() != nullptr);
                BOOST_TEST_EQ(u.v1.v, 1);
                BOOST_TEST_EQ(u.v2.v, 2);
                BOOST_TEST_EQ(ps.get<v1_t>().v, 1);
                BOOST_TEST_EQ(ps.get<v2_t>().v, 2);
                BOOST_TEST_EQ(ps.get<api>().v1.v, 1);
                BOOST_TEST_EQ(ps.get<api>().v2.v, 2);
                u.v1.v = 8;
                u.v2.v = 9;
                BOOST_TEST_EQ(ps.get<api>().v1.v, 8);
                BOOST_TEST_EQ(ps.get<api>().v2.v, 9);
            }
            {
                auto& u = ps.try_emplace<api, v1_t, v2_t>();
                BOOST_TEST_EQ(u.v1.v, 8);
                BOOST_TEST_EQ(u.v2.v, 9);
                BOOST_TEST_EQ(ps.get<api>().v1.v, 8);
                BOOST_TEST_EQ(ps.get<api>().v2.v, 9);
            }

        }
    }

    struct A
    {
        int i = 1;
    };

    struct B
    {
        char c = '2';
    };

    struct C
    {
        double d;
    };

    struct D : C
    {
        using key_type = C;
        D()
        {
            d = 3.14;
        }
    };

    struct E {};

    static int f()
    {
        return 3;
    }

    static int a0(A const& a)
    {
        return a.i;
    }

    static int a1(A const* a)
    {
        return a->i; 
    }

    static int a2(A& a)
    {
        return a.i;
    }

    static int a3(A* a)
    {
        return a->i;
    }

    static char b0(A const&, B const& b)
    {
        return b.c;
    }

    static char b1(A const*, B const* b)
    {
        return b->c; 
    }

    static char b2(A const&, B& b)
    {
        return b.c;
    }

    static char b3(A const&, B* b)
    {
        return b->c;
    }

    static double c0(C const& c)
    {
        return c.d;
    }

    void testInvoke()
    {
        polystore ps;

        BOOST_TEST_EQ(ps.find<A>(), nullptr);
        ps.emplace<A>();
        BOOST_TEST_NE(ps.find<A>(), nullptr);
        BOOST_TEST_EQ(ps.get<A>().i, 1);
        ps.insert<B>(B{});
        BOOST_TEST_NE(ps.find<B>(), nullptr);
        BOOST_TEST_EQ(ps.get<B>().c, '2');
        ps.emplace<D>();
        BOOST_TEST_NE(ps.find<D>(), nullptr);
        BOOST_TEST_EQ(ps.get<C>().d, 3.14);
        BOOST_TEST_THROWS(ps.emplace<C>(),
            std::invalid_argument);

        // invoke
        BOOST_TEST_EQ(invoke(ps, &f), 3);
        BOOST_TEST_EQ(invoke(ps, &a0), 1);
        BOOST_TEST_EQ(invoke(ps, &a1), 1);
        BOOST_TEST_EQ(invoke(ps, &a2), 1);
        BOOST_TEST_EQ(invoke(ps, &a3), 1);
        BOOST_TEST_EQ(invoke(ps, &b0), '2');
        BOOST_TEST_EQ(invoke(ps, &b1), '2');
        BOOST_TEST_EQ(invoke(ps, &b2), '2');
        BOOST_TEST_EQ(invoke(ps, &b3), '2');
        BOOST_TEST_EQ(invoke(ps, &c0), 3.14);
    }

    void run()
    {
        testFind();
        testGet();
        testEmplaceAnon();
        testEmplace();
        testTryEmplace();
        testInvoke();
    }
};

TEST_SUITE(polystore_test, "boost.capy.polystore");

} // capy
} // boost
