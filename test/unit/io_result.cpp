//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io_result.hpp>

#include <string>
#include <tuple>
#include <type_traits>

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
        io_result<> r2{make_error_code(system::errc::invalid_argument)};
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
        BOOST_TEST_EQ(r1.t1, 0u);

        // With values
        io_result<std::size_t> r2{{}, 42};
        BOOST_TEST(!r2.ec);
        BOOST_TEST_EQ(r2.t1, 42u);

        // With error
        io_result<std::size_t> r3{
            make_error_code(system::errc::invalid_argument), 10};
        BOOST_TEST(r3.ec);
        BOOST_TEST_EQ(r3.t1, 10u);

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
        BOOST_TEST_EQ(r1.t1, "hello");

        // Structured binding
        auto [ec, v] = r1;
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(v, "hello");

        // With error
        io_result<std::string> r2{
            make_error_code(system::errc::invalid_argument), "error"};
        BOOST_TEST(r2.ec);
        BOOST_TEST_EQ(r2.t1, "error");
    }

    void
    testMultiValue()
    {
        // With multiple values
        io_result<int, double, std::string> r1{
            {}, 42, 3.14, std::string("test")};
        BOOST_TEST(!r1.ec);
        BOOST_TEST_EQ(r1.t1, 42);
        BOOST_TEST_EQ(r1.t2, 3.14);
        BOOST_TEST_EQ(r1.t3, "test");

        // Structured binding
        auto [ec, a, b, c] = r1;
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(a, 42);
        BOOST_TEST_EQ(b, 3.14);
        BOOST_TEST_EQ(c, "test");

        // With error
        io_result<int, double> r2{
            make_error_code(system::errc::invalid_argument), 0, 0.0};
        BOOST_TEST(r2.ec);
        BOOST_TEST_EQ(r2.t1, 0);
        BOOST_TEST_EQ(r2.t2, 0.0);
    }

    void
    testTupleProtocol()
    {
        // Verify tuple_size
        static_assert(std::tuple_size_v<io_result<>> == 1);
        static_assert(std::tuple_size_v<io_result<std::size_t>> == 2);
        static_assert(std::tuple_size_v<io_result<int>> == 2);
        static_assert(std::tuple_size_v<io_result<int, double>> == 3);
        static_assert(std::tuple_size_v<io_result<int, double, char>> == 4);

        // Verify tuple_element
        static_assert(std::is_same_v<
            std::tuple_element_t<0, io_result<>>,
            system::error_code>);

        static_assert(std::is_same_v<
            std::tuple_element_t<0, io_result<std::size_t>>,
            system::error_code>);
        static_assert(std::is_same_v<
            std::tuple_element_t<1, io_result<std::size_t>>,
            std::size_t>);

        static_assert(std::is_same_v<
            std::tuple_element_t<0, io_result<int, double>>,
            system::error_code>);
        static_assert(std::is_same_v<
            std::tuple_element_t<1, io_result<int, double>>,
            int>);
        static_assert(std::is_same_v<
            std::tuple_element_t<2, io_result<int, double>>,
            double>);
    }

    void
    run()
    {
        testVoidResult();
        testSizeResult();
        testGenericSingleValue();
        testMultiValue();
        testTupleProtocol();
    }
};

TEST_SUITE(io_result_test, "boost.capy.io_result");

} // namespace capy
} // namespace boost
