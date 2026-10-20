//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/quick-start.adoc. Pages include the
// tagged regions; scaffolding stays outside the tags.

#include "../doc_warnings.hpp"

#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include <charconv>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <tuple>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

capy::task<int> answer()
{
    co_return 42;
}

capy::task<int> might_fail()
{
    bool fail = true;
    if (fail)
        throw std::runtime_error("might_fail");
    co_return 0;
}

// tag::error_codes[]
capy::task<capy::results<int>> parse_port(std::string_view s)
{
    int port = 0;
    auto [end, err] = std::from_chars(s.data(), s.data() + s.size(), port);
    if(err != std::errc() || end != s.data() + s.size())
        co_return {std::make_error_code(std::errc::invalid_argument), 0};
    co_return {std::error_code(), port};
}

capy::task<> connect_to_server()
{
    auto [ec, port] = co_await parse_port("8080");   // results<int>: error code first, then the value
    if(ec)
    {
        std::cerr << "Bad port: " << ec.message() << "\n";
        co_return;
    }
    std::cout << "Connecting on port " << port << "\n";
}
// end::error_codes[]

struct quick_start_test
{
    void
    testHandlingResults()
    {
        capy::thread_pool pool(1);
        auto executor = pool.get_executor();
        // tag::handler[]
        capy::run_async(executor, [](int result) {
            std::cout << "Got: " << result << "\n";
        })(answer());
        // end::handler[]
        pool.join();
    }

    void
    testHandlingErrors()
    {
        capy::thread_pool pool(1);
        auto executor = pool.get_executor();
        // tag::errors[]
        capy::run_async(executor,
            [](int result) {
                std::cout << "Success: " << result << "\n";
            },
            [](std::exception_ptr ep) {
                try {
                    if (ep) std::rethrow_exception(ep);
                } catch (std::exception const& e) {
                    std::cerr << "Error: " << e.what() << "\n";
                }
            }
        )(might_fail());
        // end::errors[]
        pool.join();
    }

    void
    testErrorCodes()
    {
        capy::thread_pool pool(1);
        auto executor = pool.get_executor();
        std::error_code ok_ec, bad_ec;
        int ok_port = 0;
        capy::run_async(executor, [&](capy::results<int> r) {
            std::tie(ok_ec, ok_port) = r;
        })(parse_port("8080"));
        capy::run_async(executor, [&](capy::results<int> r) {
            bad_ec = std::get<0>(r);
        })(parse_port("x"));
        capy::run_async(executor)(connect_to_server());
        pool.join();
        BOOST_TEST(! ok_ec);
        BOOST_TEST(ok_port == 8080);
        BOOST_TEST(bad_ec == std::errc::invalid_argument);
    }

    void
    run()
    {
        testHandlingResults();
        testHandlingErrors();
        testErrorCodes();
    }
};

} // namespace

TEST_SUITE(quick_start_test, "boost.capy.doc.quick_start");
