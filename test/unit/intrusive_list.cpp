//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/intrusive_list.hpp>

#include <utility>

#include "test_suite.hpp"

namespace boost {
namespace capy {

struct item : intrusive_list<item>::node
{
    int value;

    explicit item(int v) : value(v) {}
};

struct intrusive_list_test
{
    void
    run()
    {
        // default construction - empty list
        {
            intrusive_list<item> q;
            BOOST_TEST(q.empty());
            BOOST_TEST(q.pop_front() == nullptr);
        }

        // push and pop single element
        {
            intrusive_list<item> q;
            item w(42);
            q.push_back(&w);
            BOOST_TEST(!q.empty());
            item* p = q.pop_front();
            BOOST_TEST(p == &w);
            BOOST_TEST(p->value == 42);
            BOOST_TEST(q.empty());
            BOOST_TEST(q.pop_front() == nullptr);
        }

        // push multiple, pop in FIFO order
        {
            intrusive_list<item> q;
            item w1(1);
            item w2(2);
            item w3(3);

            q.push_back(&w1);
            q.push_back(&w2);
            q.push_back(&w3);

            BOOST_TEST(!q.empty());

            item* p1 = q.pop_front();
            BOOST_TEST(p1 == &w1);
            BOOST_TEST(p1->value == 1);

            item* p2 = q.pop_front();
            BOOST_TEST(p2 == &w2);
            BOOST_TEST(p2->value == 2);

            item* p3 = q.pop_front();
            BOOST_TEST(p3 == &w3);
            BOOST_TEST(p3->value == 3);

            BOOST_TEST(q.empty());
            BOOST_TEST(q.pop_front() == nullptr);
        }

        // interleaved push and pop
        {
            intrusive_list<item> q;
            item w1(10);
            item w2(20);
            item w3(30);

            q.push_back(&w1);
            q.push_back(&w2);

            item* p1 = q.pop_front();
            BOOST_TEST(p1 == &w1);

            q.push_back(&w3);

            item* p2 = q.pop_front();
            BOOST_TEST(p2 == &w2);

            item* p3 = q.pop_front();
            BOOST_TEST(p3 == &w3);

            BOOST_TEST(q.empty());
        }

        // reuse element after pop
        {
            intrusive_list<item> q;
            item w(100);

            q.push_back(&w);
            item* p = q.pop_front();
            BOOST_TEST(p == &w);

            // push same element again
            q.push_back(&w);
            p = q.pop_front();
            BOOST_TEST(p == &w);
            BOOST_TEST(q.empty());
        }

        // move constructor
        {
            intrusive_list<item> q1;
            item w1(1);
            item w2(2);
            q1.push_back(&w1);
            q1.push_back(&w2);

            intrusive_list<item> q2(std::move(q1));
            BOOST_TEST(q1.empty());
            BOOST_TEST(!q2.empty());

            item* p1 = q2.pop_front();
            BOOST_TEST(p1 == &w1);
            item* p2 = q2.pop_front();
            BOOST_TEST(p2 == &w2);
            BOOST_TEST(q2.empty());
        }

        // move constructor from empty
        {
            intrusive_list<item> q1;
            intrusive_list<item> q2(std::move(q1));
            BOOST_TEST(q1.empty());
            BOOST_TEST(q2.empty());
        }

        // splice non-empty into non-empty
        {
            intrusive_list<item> q1;
            intrusive_list<item> q2;
            item w1(1);
            item w2(2);
            item w3(3);
            item w4(4);

            q1.push_back(&w1);
            q1.push_back(&w2);
            q2.push_back(&w3);
            q2.push_back(&w4);

            q1.splice_back(q2);

            BOOST_TEST(q2.empty());
            BOOST_TEST(!q1.empty());

            BOOST_TEST(q1.pop_front() == &w1);
            BOOST_TEST(q1.pop_front() == &w2);
            BOOST_TEST(q1.pop_front() == &w3);
            BOOST_TEST(q1.pop_front() == &w4);
            BOOST_TEST(q1.empty());
        }

        // splice non-empty into empty
        {
            intrusive_list<item> q1;
            intrusive_list<item> q2;
            item w1(1);
            item w2(2);

            q2.push_back(&w1);
            q2.push_back(&w2);

            q1.splice_back(q2);

            BOOST_TEST(q2.empty());
            BOOST_TEST(!q1.empty());

            BOOST_TEST(q1.pop_front() == &w1);
            BOOST_TEST(q1.pop_front() == &w2);
            BOOST_TEST(q1.empty());
        }

        // splice empty into non-empty
        {
            intrusive_list<item> q1;
            intrusive_list<item> q2;
            item w1(1);

            q1.push_back(&w1);

            q1.splice_back(q2);

            BOOST_TEST(q2.empty());
            BOOST_TEST(!q1.empty());
            BOOST_TEST(q1.pop_front() == &w1);
            BOOST_TEST(q1.empty());
        }

        // splice empty into empty
        {
            intrusive_list<item> q1;
            intrusive_list<item> q2;

            q1.splice_back(q2);

            BOOST_TEST(q1.empty());
            BOOST_TEST(q2.empty());
        }

        // remove only element
        {
            intrusive_list<item> q;
            item w(1);
            q.push_back(&w);
            q.remove(&w);
            BOOST_TEST(q.empty());
            BOOST_TEST(q.pop_front() == nullptr);
        }

        // remove from head
        {
            intrusive_list<item> q;
            item w1(1);
            item w2(2);
            item w3(3);
            q.push_back(&w1);
            q.push_back(&w2);
            q.push_back(&w3);

            q.remove(&w1);

            BOOST_TEST(!q.empty());
            BOOST_TEST(q.pop_front() == &w2);
            BOOST_TEST(q.pop_front() == &w3);
            BOOST_TEST(q.empty());
        }

        // remove from tail
        {
            intrusive_list<item> q;
            item w1(1);
            item w2(2);
            item w3(3);
            q.push_back(&w1);
            q.push_back(&w2);
            q.push_back(&w3);

            q.remove(&w3);

            BOOST_TEST(!q.empty());
            BOOST_TEST(q.pop_front() == &w1);
            BOOST_TEST(q.pop_front() == &w2);
            BOOST_TEST(q.empty());
        }

        // remove from middle
        {
            intrusive_list<item> q;
            item w1(1);
            item w2(2);
            item w3(3);
            q.push_back(&w1);
            q.push_back(&w2);
            q.push_back(&w3);

            q.remove(&w2);

            BOOST_TEST(!q.empty());
            BOOST_TEST(q.pop_front() == &w1);
            BOOST_TEST(q.pop_front() == &w3);
            BOOST_TEST(q.empty());
        }

        // reuse element after remove
        {
            intrusive_list<item> q;
            item w(100);

            q.push_back(&w);
            q.remove(&w);
            BOOST_TEST(q.empty());

            // push same element again
            q.push_back(&w);
            item* p = q.pop_front();
            BOOST_TEST(p == &w);
            BOOST_TEST(q.empty());
        }
    }
};

TEST_SUITE(
    intrusive_list_test,
    "boost.capy.intrusive_list");

} // capy
} // boost
