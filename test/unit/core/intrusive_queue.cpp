//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/core/intrusive_queue.hpp>

#include <utility>

#include "test_suite.hpp"

namespace boost {
namespace capy {

struct queue_item : intrusive_queue<queue_item>::node
{
    int value;

    explicit queue_item(int v) : value(v) {}
};

struct intrusive_queue_test
{
    void
    run()
    {
        // default construction - empty queue
        {
            intrusive_queue<queue_item> q;
            BOOST_TEST(q.empty());
            BOOST_TEST(q.pop() == nullptr);
        }

        // push and pop single element
        {
            intrusive_queue<queue_item> q;
            queue_item w(42);
            q.push(&w);
            BOOST_TEST(!q.empty());
            queue_item* p = q.pop();
            BOOST_TEST(p == &w);
            BOOST_TEST(p->value == 42);
            BOOST_TEST(q.empty());
            BOOST_TEST(q.pop() == nullptr);
        }

        // push multiple, pop in FIFO order
        {
            intrusive_queue<queue_item> q;
            queue_item w1(1);
            queue_item w2(2);
            queue_item w3(3);

            q.push(&w1);
            q.push(&w2);
            q.push(&w3);

            BOOST_TEST(!q.empty());

            queue_item* p1 = q.pop();
            BOOST_TEST(p1 == &w1);
            BOOST_TEST(p1->value == 1);

            queue_item* p2 = q.pop();
            BOOST_TEST(p2 == &w2);
            BOOST_TEST(p2->value == 2);

            queue_item* p3 = q.pop();
            BOOST_TEST(p3 == &w3);
            BOOST_TEST(p3->value == 3);

            BOOST_TEST(q.empty());
            BOOST_TEST(q.pop() == nullptr);
        }

        // interleaved push and pop
        {
            intrusive_queue<queue_item> q;
            queue_item w1(10);
            queue_item w2(20);
            queue_item w3(30);

            q.push(&w1);
            q.push(&w2);

            queue_item* p1 = q.pop();
            BOOST_TEST(p1 == &w1);

            q.push(&w3);

            queue_item* p2 = q.pop();
            BOOST_TEST(p2 == &w2);

            queue_item* p3 = q.pop();
            BOOST_TEST(p3 == &w3);

            BOOST_TEST(q.empty());
        }

        // reuse element after pop
        {
            intrusive_queue<queue_item> q;
            queue_item w(100);

            q.push(&w);
            queue_item* p = q.pop();
            BOOST_TEST(p == &w);

            // push same element again
            q.push(&w);
            p = q.pop();
            BOOST_TEST(p == &w);
            BOOST_TEST(q.empty());
        }

        // move constructor
        {
            intrusive_queue<queue_item> q1;
            queue_item w1(1);
            queue_item w2(2);
            q1.push(&w1);
            q1.push(&w2);

            intrusive_queue<queue_item> q2(std::move(q1));
            BOOST_TEST(q1.empty());
            BOOST_TEST(!q2.empty());

            queue_item* p1 = q2.pop();
            BOOST_TEST(p1 == &w1);
            queue_item* p2 = q2.pop();
            BOOST_TEST(p2 == &w2);
            BOOST_TEST(q2.empty());
        }

        // move constructor from empty
        {
            intrusive_queue<queue_item> q1;
            intrusive_queue<queue_item> q2(std::move(q1));
            BOOST_TEST(q1.empty());
            BOOST_TEST(q2.empty());
        }

        // splice non-empty into non-empty
        {
            intrusive_queue<queue_item> q1;
            intrusive_queue<queue_item> q2;
            queue_item w1(1);
            queue_item w2(2);
            queue_item w3(3);
            queue_item w4(4);

            q1.push(&w1);
            q1.push(&w2);
            q2.push(&w3);
            q2.push(&w4);

            q1.splice(q2);

            BOOST_TEST(q2.empty());
            BOOST_TEST(!q1.empty());

            BOOST_TEST(q1.pop() == &w1);
            BOOST_TEST(q1.pop() == &w2);
            BOOST_TEST(q1.pop() == &w3);
            BOOST_TEST(q1.pop() == &w4);
            BOOST_TEST(q1.empty());
        }

        // splice non-empty into empty
        {
            intrusive_queue<queue_item> q1;
            intrusive_queue<queue_item> q2;
            queue_item w1(1);
            queue_item w2(2);

            q2.push(&w1);
            q2.push(&w2);

            q1.splice(q2);

            BOOST_TEST(q2.empty());
            BOOST_TEST(!q1.empty());

            BOOST_TEST(q1.pop() == &w1);
            BOOST_TEST(q1.pop() == &w2);
            BOOST_TEST(q1.empty());
        }

        // splice empty into non-empty
        {
            intrusive_queue<queue_item> q1;
            intrusive_queue<queue_item> q2;
            queue_item w1(1);

            q1.push(&w1);

            q1.splice(q2);

            BOOST_TEST(q2.empty());
            BOOST_TEST(!q1.empty());
            BOOST_TEST(q1.pop() == &w1);
            BOOST_TEST(q1.empty());
        }

        // splice empty into empty
        {
            intrusive_queue<queue_item> q1;
            intrusive_queue<queue_item> q2;

            q1.splice(q2);

            BOOST_TEST(q1.empty());
            BOOST_TEST(q2.empty());
        }
    }
};

TEST_SUITE(
    intrusive_queue_test,
    "boost.capy.intrusive_queue");

} // capy
} // boost
