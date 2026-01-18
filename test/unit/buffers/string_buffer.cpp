//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/string_buffer.hpp>

#include <boost/capy/buffers/dynamic_buffer.hpp>
#include <boost/static_assert.hpp>

#include "test_buffers.hpp"

namespace boost {
namespace capy {

BOOST_STATIC_ASSERT(is_DynamicBuffer<string_buffer>::value);

struct string_buffer_test
{
    void
    testMembers()
    {
        std::string s;

        // ~string_buffer
        {
            s = "";
            string_buffer b(&s);
            BOOST_TEST(s.empty());
        }

        // string_buffer
        {
            std::string s0;
            {
                string_buffer b0(&s0);
                string_buffer b1(std::move(b0));
                auto n = copy(
                    b1.prepare(5),
                    make_buffer("12345", 5));
                BOOST_TEST_EQ(n, 5);
                b1.commit(5);
            }
            BOOST_TEST_EQ(s0, "12345");
        }

        // string_buffer(std::string)
        {
            s = "";
            string_buffer b(&s);
            BOOST_TEST_EQ(
                b.max_size(), s.max_size());
        }

        // string_buffer(std::string, std::size_t)
        // max_size()
        {
            s = "";
            string_buffer b(&s, 20);
            BOOST_TEST_EQ(b.max_size(), 20);
        }

        // size()
        {
            s = "1234";
            string_buffer b(&s);
            BOOST_TEST_EQ(b.size(), 4);
        }

        // capacity()
        {
            {
                s = "";
                s.reserve(30);
                string_buffer b(&s);
                BOOST_TEST_GE(b.capacity(), 30);
            }
            {
                s = "";
                s.reserve(30);
                string_buffer b(&s, 10);
                BOOST_TEST_GE(b.capacity(), 10);
            }
        }

        // data()
        {
            s = "1234";
            string_buffer b(&s);
            BOOST_TEST_EQ(
                test::make_string(b.data()),
                "1234");
        }

        // prepare()
        {
            {
                string_buffer b(&s, 3);
                BOOST_TEST_THROWS(
                    b.prepare(5),
                    std::invalid_argument);
            }
            {
                s = std::string();
                string_buffer b(&s);
                auto dest = b.prepare(10);
                BOOST_TEST_GE(s.capacity(),
                    buffer_size(dest));
            }
            {
                s = std::string();
                string_buffer b(&s);
                b.prepare(10);
                auto dest = b.prepare(10);
                BOOST_TEST_EQ(
                    buffer_size(dest),
                    10);
            }
        }

        // commit()
        {
            s = "";
            {
                string_buffer b(&s);
                auto n = copy(
                    b.prepare(5),
                    make_buffer("12345", 5));
                BOOST_TEST_EQ(n, 5);
                b.commit(3);
                BOOST_TEST_EQ(b.size(), 3);
            }
            BOOST_TEST_EQ(s, "123");
        }

        // consume()
        {
            {
                s = "12345";
                {
                    string_buffer b(&s);
                    b.consume(2);
                }
                BOOST_TEST_EQ(s, "345");
            }
            {
                s = "12345";
                {
                    string_buffer b(&s);
                    b.consume(5);
                    BOOST_TEST_EQ(
                        buffer_size(b.data()), 0);
                }
                BOOST_TEST(s.empty());
            }
        }
    }

    void
    run()
    {
        testMembers();
    }
};

TEST_SUITE(
    string_buffer_test,
    "boost.capy.buffers.string_buffer");

} // capy
} // boost
