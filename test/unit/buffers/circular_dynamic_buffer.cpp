//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/circular_dynamic_buffer.hpp>

#include <boost/capy/concept/dynamic_buffer.hpp>

#include "test/unit/test_dynamic_buffer.hpp"
#include "test_buffers.hpp"

namespace boost {
namespace capy {

static_assert(DynamicBuffer<circular_dynamic_buffer>);

struct circular_dynamic_buffer_test
{
    void
    testMembers()
    {
        std::string pat = test_pattern();

        // circular_dynamic_buffer()
        {
            circular_dynamic_buffer cb;
            BOOST_TEST_EQ(cb.size(), 0);
        }

        // circular_dynamic_buffer( void*, std::size_t )
        {
            circular_dynamic_buffer cb(
                &pat[0], pat.size());
            BOOST_TEST_EQ(cb.size(), 0);
            BOOST_TEST_EQ(cb.capacity(), pat.size());
            BOOST_TEST_EQ(cb.max_size(), pat.size());
        }

        // circular_dynamic_buffer( void*, std::size_t, std:size_t )
        {
            circular_dynamic_buffer cb(
                &pat[0], pat.size(), 6);
            BOOST_TEST_EQ(cb.size(), 6);
            BOOST_TEST_EQ(
                cb.capacity(), pat.size() - 6);
            BOOST_TEST_EQ(cb.max_size(), pat.size());
            BOOST_TEST_EQ(
                test::make_string(cb.data()),
                pat.substr(0, 6));
        }
        {
            BOOST_TEST_THROWS(
                circular_dynamic_buffer(
                    &pat[0], pat.size(), 600),
                std::exception);
        }

        // circular_dynamic_buffer( circular_dynamic_buffer const& )
        {
            circular_dynamic_buffer cb0(&pat[0], pat.size());
            circular_dynamic_buffer cb1(cb0);
            BOOST_TEST_EQ(cb1.size(), cb0.size());
            BOOST_TEST_EQ(cb1.capacity(), cb0.capacity());
            BOOST_TEST_EQ(cb1.max_size(), cb0.max_size());
        }

        // operator=( circular_dynamic_buffer const& )
        {
            circular_dynamic_buffer cb0(&pat[0], pat.size());
            circular_dynamic_buffer cb1;
            cb1 = cb0;
            BOOST_TEST_EQ(cb1.size(), cb0.size());
            BOOST_TEST_EQ(cb1.capacity(), cb0.capacity());
            BOOST_TEST_EQ(cb1.max_size(), cb0.max_size());
        }

        // prepare( std::size_t )
        {
            circular_dynamic_buffer cb(&pat[0], pat.size());
            BOOST_TEST_THROWS(
                cb.prepare(cb.capacity() + 1),
                std::length_error);
        }

        // commit( std::size_t )
        {
            circular_dynamic_buffer cb(&pat[0], pat.size());
            auto n = pat.size() / 2;
            cb.prepare(pat.size());
            cb.commit(n);
            BOOST_TEST_EQ(
                test::make_string(cb.data()),
                pat.substr(0, n));
        }
    }

    void
    testGrind()
    {
        std::string storage(64, '\0');
        auto r = test::grind_dynamic_buffer([&] {
            std::fill(storage.begin(), storage.end(), '\0');
            return circular_dynamic_buffer(&storage[0], storage.size());
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
    circular_dynamic_buffer_test,
    "boost.capy.buffers.circular_dynamic_buffer");

} // capy
} // boost
