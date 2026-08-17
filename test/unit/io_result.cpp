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
#include <tuple>

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
        BOOST_TEST(!std::get<0>(r1));

        // With error
        io_result<> r2{make_error_code(std::errc::invalid_argument)};
        BOOST_TEST(std::get<0>(r2));

        // Structured binding
        auto [ec] = r1;
        BOOST_TEST(!ec);
    }

    void
    testSizeResult()
    {
        // Default construction
        io_result<std::size_t> r1;
        BOOST_TEST(!std::get<0>(r1));
        BOOST_TEST_EQ(std::get<1>(r1), 0u);

        // With values
        io_result<std::size_t> r2{std::error_code(), 42};
        BOOST_TEST(!std::get<0>(r2));
        BOOST_TEST_EQ(std::get<1>(r2), 42u);

        // With error
        io_result<std::size_t> r3{
            make_error_code(std::errc::invalid_argument), 10};
        BOOST_TEST(std::get<0>(r3));
        BOOST_TEST_EQ(std::get<1>(r3), 10u);

        // Structured binding
        auto [ec, n] = r2;
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(n, 42u);
    }

    void
    testGenericSingleValue()
    {
        // With string value
        io_result<std::string> r1{std::error_code(), "hello"};
        BOOST_TEST(!std::get<0>(r1));
        BOOST_TEST_EQ(std::get<1>(r1), "hello");

        // Structured binding
        auto [ec, v] = r1;
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(v, "hello");

        // With error
        io_result<std::string> r2{
            make_error_code(std::errc::invalid_argument), "error"};
        BOOST_TEST(std::get<0>(r2));
        BOOST_TEST_EQ(std::get<1>(r2), "error");
    }

    void
    testMultiValue()
    {
        // With multiple values
        io_result<int, double, std::string> r1{
            {}, 42, 3.14, std::string("test")};
        BOOST_TEST(!std::get<0>(r1));
        BOOST_TEST_EQ(std::get<1>(r1), 42);
        BOOST_TEST_EQ(std::get<2>(r1), 3.14);
        BOOST_TEST_EQ(std::get<3>(r1), "test");

        // Structured binding
        auto [ec, a, b, c] = r1;
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(a, 42);
        BOOST_TEST_EQ(b, 3.14);
        BOOST_TEST_EQ(c, "test");

        // With error
        io_result<int, double> r2{
            make_error_code(std::errc::invalid_argument), 0, 0.0};
        BOOST_TEST(std::get<0>(r2));
        BOOST_TEST_EQ(std::get<1>(r2), 0);
        BOOST_TEST_EQ(std::get<2>(r2), 0.0);
    }

    void
    testFourPlusArgs()
    {
        // Verify no arity limit
        io_result<int, double, std::string, bool> r1{
            {}, 1, 2.5, std::string("hi"), true};
        BOOST_TEST(!std::get<0>(r1));
        BOOST_TEST_EQ(std::get<1>(r1), 1);
        BOOST_TEST_EQ(std::get<2>(r1), 2.5);
        BOOST_TEST_EQ(std::get<3>(r1), "hi");
        BOOST_TEST_EQ(std::get<4>(r1), true);

        // Structured binding
        auto [ec, a, b, c, d] = r1;
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(a, 1);
        BOOST_TEST_EQ(b, 2.5);
        BOOST_TEST_EQ(c, "hi");
        BOOST_TEST_EQ(d, true);

        // Default construction
        io_result<int, double, std::string, bool> r2;
        BOOST_TEST(!std::get<0>(r2));
        BOOST_TEST_EQ(std::get<1>(r2), 0);
        BOOST_TEST_EQ(std::get<4>(r2), false);
    }

    void
    testTie()
    {
        // std::tie rebinds into existing variables, and
        // avoids structured bindings entirely
        std::error_code ec;
        std::size_t n = 0;
        std::tie(ec, n) = io_result<std::size_t>{std::error_code(), 42};
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(n, 42u);

        std::tie(ec, n) = io_result<std::size_t>{
            make_error_code(std::errc::invalid_argument), 7};
        BOOST_TEST(ec);
        BOOST_TEST_EQ(n, 7u);

        // Partial rebinding with std::ignore
        std::tie(ec, std::ignore) =
            io_result<std::size_t>{std::error_code(), 99};
        BOOST_TEST(!ec);
        BOOST_TEST_EQ(n, 7u);
    }

    void
    testApply()
    {
        io_result<std::size_t> r{std::error_code(), 42};
        auto sum = std::apply(
            [](std::error_code ec, std::size_t n)
            {
                return ec ? 0u : n;
            }, r);
        BOOST_TEST_EQ(sum, 42u);
    }

    void
    testTupleInterop()
    {
        // Comparison
        io_result<std::size_t> r1{std::error_code(), 42};
        io_result<std::size_t> r2{std::error_code(), 42};
        BOOST_TEST(r1 == r2);

        // Assignment from a plain tuple
        r1 = std::tuple<std::error_code, std::size_t>{
            make_error_code(std::errc::invalid_argument), 10};
        BOOST_TEST(r1 != r2);
        BOOST_TEST_EQ(std::get<1>(r1), 10u);

        // tuple_cat
        auto joined = std::tuple_cat(
            r2, std::tuple<int>{7});
        BOOST_TEST_EQ(std::get<2>(joined), 7);
    }

    void
    testTraits()
    {
        // Outcome detection is structural: any tuple with a
        // leading error_code qualifies, no others do
        static_assert(detail::is_io_result_v<io_result<>>);
        static_assert(detail::is_io_result_v<
            io_result<std::size_t>>);
        static_assert(detail::is_io_result_v<
            std::tuple<std::error_code, int, double>>);
        static_assert(!detail::is_io_result_v<
            std::tuple<int, std::error_code>>);
        static_assert(!detail::is_io_result_v<std::tuple<>>);
        static_assert(!detail::is_io_result_v<std::error_code>);
    }

    void
    run()
    {
        testVoidResult();
        testSizeResult();
        testGenericSingleValue();
        testMultiValue();
        testFourPlusArgs();
        testTie();
        testApply();
        testTupleInterop();
        testTraits();
    }
};

TEST_SUITE(io_result_test, "boost.capy.io_result");

} // namespace capy
} // namespace boost
