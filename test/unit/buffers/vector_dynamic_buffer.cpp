//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/vector_dynamic_buffer.hpp>

#include <boost/capy/concept/dynamic_buffer.hpp>

#include "test/unit/test_dynamic_buffer.hpp"
#include "test_buffers.hpp"

namespace boost {
namespace capy {

static_assert(DynamicBuffer<vector_dynamic_buffer>);

struct vector_dynamic_buffer_test
{
    void
    testMembers()
    {
        std::vector<unsigned char> v;

        // ~vector_dynamic_buffer
        {
            v.clear();
            vector_dynamic_buffer b(&v);
            BOOST_TEST(v.empty());
        }

        // vector_dynamic_buffer( move constructor )
        {
            std::vector<unsigned char> v0;
            {
                vector_dynamic_buffer b0(&v0);
                vector_dynamic_buffer b1(std::move(b0));
                auto n = buffer_copy(
                    b1.prepare(5),
                    make_buffer("12345", 5));
                BOOST_TEST_EQ(n, 5);
                b1.commit(5);
            }
            BOOST_TEST_EQ(v0.size(), 5);
            BOOST_TEST(std::equal(
                v0.begin(), v0.end(), "12345"));
        }
        {
            // move transfers in_size_
            std::vector<unsigned char> v0;
            {
                vector_dynamic_buffer b0(&v0);
                buffer_copy(b0.prepare(5), make_buffer("12345", 5));
                b0.commit(5);
                BOOST_TEST_EQ(b0.size(), 5);
                vector_dynamic_buffer b1(std::move(b0));
                BOOST_TEST_EQ(b1.size(), 5);
                BOOST_TEST_EQ(
                    test::make_string(b1.data()), "12345");
            }
            BOOST_TEST_EQ(v0.size(), 5);
        }

        // vector_dynamic_buffer( vector_type* )
        {
            v.clear();
            vector_dynamic_buffer b(&v);
            BOOST_TEST_EQ(
                b.max_size(), v.max_size());
        }

        // vector_dynamic_buffer( vector_type*, std::size_t )
        // max_size()
        {
            v.clear();
            vector_dynamic_buffer b(&v, 20);
            BOOST_TEST_EQ(b.max_size(), 20);
        }

        // size()
        {
            v.assign({'1', '2', '3', '4'});
            vector_dynamic_buffer b(&v);
            BOOST_TEST_EQ(b.size(), 4);
        }

        // capacity()
        {
            {
                v.clear();
                v.reserve(30);
                vector_dynamic_buffer b(&v);
                BOOST_TEST_GE(b.capacity(), 30);
            }
            {
                v.clear();
                v.reserve(30);
                vector_dynamic_buffer b(&v, 10);
                BOOST_TEST_GE(b.capacity(), 10);
            }
        }

        // data()
        {
            v.assign({'1', '2', '3', '4'});
            vector_dynamic_buffer b(&v);
            BOOST_TEST_EQ(
                test::make_string(b.data()),
                "1234");
        }

        // prepare()
        {
            {
                v.clear();
                vector_dynamic_buffer b(&v, 3);
                BOOST_TEST_THROWS(
                    b.prepare(5),
                    std::invalid_argument);
            }
            {
                v.clear();
                vector_dynamic_buffer b(&v);
                auto dest = b.prepare(10);
                BOOST_TEST_GE(v.capacity(),
                    buffer_size(dest));
            }
            {
                v.clear();
                vector_dynamic_buffer b(&v);
                b.prepare(10);
                auto dest = b.prepare(10);
                BOOST_TEST_EQ(
                    buffer_size(dest),
                    10);
            }
        }

        // commit()
        {
            v.clear();
            {
                vector_dynamic_buffer b(&v);
                auto n = buffer_copy(
                    b.prepare(5),
                    make_buffer("12345", 5));
                BOOST_TEST_EQ(n, 5);
                b.commit(3);
                BOOST_TEST_EQ(b.size(), 3);
            }
            BOOST_TEST_EQ(v.size(), 3);
            BOOST_TEST(std::equal(
                v.begin(), v.end(), "123"));
        }

        // consume()
        {
            {
                v.assign({'1', '2', '3', '4', '5'});
                {
                    vector_dynamic_buffer b(&v);
                    b.consume(2);
                }
                BOOST_TEST_EQ(v.size(), 3);
                BOOST_TEST(std::equal(
                    v.begin(), v.end(), "345"));
            }
            {
                v.assign({'1', '2', '3', '4', '5'});
                {
                    vector_dynamic_buffer b(&v);
                    b.consume(5);
                    BOOST_TEST_EQ(
                        buffer_size(b.data()), 0);
                }
                BOOST_TEST(v.empty());
            }
        }
    }

    void
    testGrind()
    {
        std::vector<unsigned char> v;
        auto r = test::grind_dynamic_buffer([&] {
            v.clear();
            return vector_dynamic_buffer(&v);
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
    vector_dynamic_buffer_test,
    "boost.capy.buffers.vector_dynamic_buffer");

} // capy
} // boost
