//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/detail/type_id.hpp>

#include <unordered_map>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace detail {

struct type_id_test
{
    void
    run()
    {
        // Distinct types have distinct ids
        {
            BOOST_TEST(type_id<int>() != type_id<float>());
            BOOST_TEST(type_id<int>() != type_id<double>());
        }

        // Same type has same id
        {
            BOOST_TEST(type_id<int>() == type_id<int>());
        }

        // top-level cv-qualifiers are ignored (matches typeid behavior)
        {
            BOOST_TEST(type_id<int>() == type_id<int const>());
            BOOST_TEST(type_id<int>() == type_id<int volatile>());
            BOOST_TEST(type_id<int>() == type_id<int const volatile>());
        }

        // before() provides ordering
        {
            auto const& a = type_id<int>();
            auto const& b = type_id<float>();
            BOOST_TEST(a.before(b) || b.before(a));
            BOOST_TEST(!a.before(a));
        }

        // hash_code() works
        {
            BOOST_TEST(type_id<int>().hash_code() == type_id<int>().hash_code());
            BOOST_TEST(type_id<int>().hash_code() != type_id<float>().hash_code());
        }

        // type_index comparisons
        {
            type_index ti_int(type_id<int>());
            type_index ti_float(type_id<float>());
            type_index ti_int2(type_id<int>());

            BOOST_TEST(ti_int == ti_int2);
            BOOST_TEST(ti_int != ti_float);
            BOOST_TEST((ti_int < ti_float) || (ti_float < ti_int));
        }

        // type_index as map key
        {
            std::unordered_map<type_index, int> type_map;
            type_map[type_id<int>()] = 1;
            type_map[type_id<float>()] = 2;
            BOOST_TEST(type_map[type_id<int>()] == 1);
            BOOST_TEST(type_map[type_id<float>()] == 2);
        }

#if BOOST_CAPY_NO_RTTI
        // name() returns type name (custom impl)
        {
            BOOST_TEST(type_id<int>().name() == "int");
            BOOST_TEST(type_id<float>().name() == "float");
        }

        // type_name variable template (custom impl only)
        {
            BOOST_TEST(type_name<int> == "int");
            BOOST_TEST(type_name<float> == "float");
        }
#endif
    }
};

TEST_SUITE(
    type_id_test,
    "boost.capy.detail.type_id");

} // detail
} // capy
} // boost
