//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io_result.hpp>

#include <string>

#include "test_suite.hpp"

namespace boost {
namespace capy {

struct io_result_test
{
    void
    testVoidResult()
    {
        // Default construction
        io_result<> r1;
        BOOST_TEST(!r1.ec);

        // With error
        io_result<> r2{make_error_code(std::errc::invalid_argument)};
        BOOST_TEST(r2.ec);

        // Structured binding
        auto [ec] = r1;
        BOOST_TEST(!ec);
    }

    void
    testSizeResult()
    {
        // Default construction
        io_result<std::size_t> r1;
        BOOST_TEST(!r1.ec);
        BOOST_TEST_EQ(std::get<0>(r1.values), 0u);

        // With values
        io_result<std::size_t> r2{{}, 42};
        BOOST_TEST(!r2.ec);
        BOOST_TEST_EQ(std::get<0>(r2.values), 42u);

        // With error
        io_result<std::size_t> r3{
            make_error_code(std::errc::invalid_argument), 10};
        BOOST_TEST(r3.ec);
        BOOST_TEST_EQ(std::get<0>(r3.values), 10u);

        // Structured binding
        auto [ec, n] = r2;
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(n, 42u);
    }

    void
    testGenericSingleValue()
    {
        // With string value
        io_result<std::string> r1{{}, "hello"};
        BOOST_TEST(!r1.ec);
        BOOST_TEST_EQ(std::get<0>(r1.values), "hello");

        // Structured binding
        auto [ec, v] = r1;
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(v, "hello");

        // With error
        io_result<std::string> r2{
            make_error_code(std::errc::invalid_argument), "error"};
        BOOST_TEST(r2.ec);
        BOOST_TEST_EQ(std::get<0>(r2.values), "error");
    }

    void
    testMultiValue()
    {
        // With multiple values
        io_result<int, double, std::string> r1{
            {}, 42, 3.14, std::string("test")};
        BOOST_TEST(!r1.ec);
        BOOST_TEST_EQ(std::get<0>(r1.values), 42);
        BOOST_TEST_EQ(std::get<1>(r1.values), 3.14);
        BOOST_TEST_EQ(std::get<2>(r1.values), "test");

        // Structured binding
        auto [ec, a, b, c] = r1;
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(a, 42);
        BOOST_TEST_EQ(b, 3.14);
        BOOST_TEST_EQ(c, "test");

        // With error
        io_result<int, double> r2{
            make_error_code(std::errc::invalid_argument), 0, 0.0};
        BOOST_TEST(r2.ec);
        BOOST_TEST_EQ(std::get<0>(r2.values), 0);
        BOOST_TEST_EQ(std::get<1>(r2.values), 0.0);
    }

    void
    testFourPlusArgs()
    {
        // Verify no arity limit
        io_result<int, double, std::string, bool> r1{
            {}, 1, 2.5, std::string("hi"), true};
        BOOST_TEST(!r1.ec);
        BOOST_TEST_EQ(std::get<0>(r1.values), 1);
        BOOST_TEST_EQ(std::get<1>(r1.values), 2.5);
        BOOST_TEST_EQ(std::get<2>(r1.values), "hi");
        BOOST_TEST_EQ(std::get<3>(r1.values), true);

        // Structured binding
        auto [ec, a, b, c, d] = r1;
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(a, 1);
        BOOST_TEST_EQ(b, 2.5);
        BOOST_TEST_EQ(c, "hi");
        BOOST_TEST_EQ(d, true);

        // Default construction
        io_result<int, double, std::string, bool> r2;
        BOOST_TEST(!r2.ec);
        BOOST_TEST_EQ(std::get<0>(r2.values), 0);
        BOOST_TEST_EQ(std::get<3>(r2.values), false);
    }

    void
    run()
    {
        testVoidResult();
        testSizeResult();
        testGenericSingleValue();
        testMultiValue();
        testFourPlusArgs();
    }
};

TEST_SUITE(io_result_test, "boost.capy.io_result");

} // namespace capy
} // namespace boost
