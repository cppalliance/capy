//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/string_dynamic_buffer.hpp>

#include <boost/capy/concept/dynamic_buffer.hpp>

#include "test/unit/test_dynamic_buffer.hpp"
#include "test_buffers.hpp"

namespace boost {
namespace capy {

static_assert(DynamicBuffer<string_dynamic_buffer>);

struct string_dynamic_buffer_test
{
    void
    testMembers()
    {
        std::string s;

        // ~string_dynamic_buffer
        {
            s = "";
            string_dynamic_buffer b(&s);
            BOOST_TEST(s.empty());
        }

        // string_dynamic_buffer (move constructor)
        {
            std::string s0;
            {
                string_dynamic_buffer b0(&s0);
                string_dynamic_buffer b1(std::move(b0));
                auto n = buffer_copy(
                    b1.prepare(5),
                    make_buffer("12345", 5));
                BOOST_TEST_EQ(n, 5);
                b1.commit(5);
            }
            BOOST_TEST_EQ(s0, "12345");
        }
        {
            // move transfers in_size_
            std::string s0;
            {
                string_dynamic_buffer b0(&s0);
                buffer_copy(b0.prepare(5), make_buffer("12345", 5));
                b0.commit(5);
                BOOST_TEST_EQ(b0.size(), 5);
                string_dynamic_buffer b1(std::move(b0));
                BOOST_TEST_EQ(b1.size(), 5);
                BOOST_TEST_EQ(
                    test::make_string(b1.data()), "12345");
            }
            BOOST_TEST_EQ(s0, "12345");
        }

        // string_dynamic_buffer(std::string)
        {
            s = "";
            string_dynamic_buffer b(&s);
            BOOST_TEST_EQ(
                b.max_size(), s.max_size());
        }

        // string_dynamic_buffer(std::string, std::size_t)
        // max_size()
        {
            s = "";
            string_dynamic_buffer b(&s, 20);
            BOOST_TEST_EQ(b.max_size(), 20);
        }

        // size()
        {
            s = "1234";
            string_dynamic_buffer b(&s);
            BOOST_TEST_EQ(b.size(), 4);
        }

        // capacity()
        {
            {
                s = "";
                s.reserve(30);
                string_dynamic_buffer b(&s);
                BOOST_TEST_GE(b.capacity(), 30);
            }
            {
                s = "";
                s.reserve(30);
                string_dynamic_buffer b(&s, 10);
                BOOST_TEST_GE(b.capacity(), 10);
            }
        }

        // data()
        {
            s = "1234";
            string_dynamic_buffer b(&s);
            BOOST_TEST_EQ(
                test::make_string(b.data()),
                "1234");
        }

        // prepare()
        {
            {
                string_dynamic_buffer b(&s, 3);
                BOOST_TEST_THROWS(
                    b.prepare(5),
                    std::invalid_argument);
            }
            {
                s = std::string();
                string_dynamic_buffer b(&s);
                auto dest = b.prepare(10);
                BOOST_TEST_GE(s.capacity(),
                    buffer_size(dest));
            }
            {
                s = std::string();
                string_dynamic_buffer b(&s);
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
                string_dynamic_buffer b(&s);
                auto n = buffer_copy(
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
                    string_dynamic_buffer b(&s);
                    b.consume(2);
                }
                BOOST_TEST_EQ(s, "345");
            }
            {
                s = "12345";
                {
                    string_dynamic_buffer b(&s);
                    b.consume(5);
                    BOOST_TEST_EQ(
                        buffer_size(b.data()), 0);
                }
                BOOST_TEST(s.empty());
            }
        }
    }

    void
    testGrind()
    {
        std::string s;
        auto r = test::grind_dynamic_buffer([&] {
            s.clear();
            return string_dynamic_buffer(&s);
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testMembers();
        testGrind();
    }
};

TEST_SUITE(
    string_dynamic_buffer_test,
    "boost.capy.buffers.string_dynamic_buffer");

} // capy
} // boost
