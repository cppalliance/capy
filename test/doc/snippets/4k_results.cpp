//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4k.results.adoc. Pages
// include the tagged regions; scaffolding stays outside the tags.

#include "../doc_warnings.hpp"

// GCC gives false positive -Wmaybe-uninitialized on structured bindings
// via the tuple protocol inside coroutine frames.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

// tag::include_results[]
#include <boost/capy/results.hpp>
// end::include_results[]

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/read.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/read_stream.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

namespace producing {

// tag::produce[]
// `results<Ts...>` holds an error code `ec` plus zero or more payload
// values. A task returning one reports failure without throwing.

capy::task<capy::results<>> ensure_ready(bool ready)
{
    if(! ready)
        co_return std::make_error_code(std::errc::not_connected);  // ec converts
    co_return {};                                             // success
}

capy::task<capy::results<std::size_t>> count_ready(bool ready)
{
    using result = capy::results<std::size_t>;
    if(! ready)
        co_return result{std::make_error_code(std::errc::not_connected), 0};
    co_return result{std::error_code(), 42};  // success, carrying a value
}

capy::task<> use_them()
{
    // `results` models the tuple protocol: ec first, then the payloads.
    auto [ec, n] = co_await count_ready(true);
    if(ec)
        co_return;  // always check ec first
    (void)n;        // n is only meaningful when ec is falsy
}
// end::produce[]

} // namespace producing

// tag::consume[]
capy::task<std::size_t> read_once(capy::test::read_stream& rs)
{
    char buf[64];
    // read_some returns results<std::size_t>: an error code, then a byte count
    auto [ec, n] = co_await rs.read_some(capy::make_buffer(buf));
    if(ec)          // always test the error code first
        co_return 0;
    co_return n;    // n is meaningful because ec is clear
}
// end::consume[]

// tag::partial[]
capy::task<> read_header(capy::test::read_stream& rs)
{
    char buf[8];
    // read keeps going until buf is full, or a condition stops it
    auto [ec, n] = co_await capy::read(rs, capy::make_buffer(buf));
    if(ec == capy::cond::eof)
    {
        // The stream ended early. n still counts the bytes
        // placed in buf before it did.
        std::string_view got(buf, n);
        (void)got;
    }
}
// end::partial[]

// tag::tie[]
capy::task<std::string> read_all(capy::test::read_stream& rs)
{
    std::string body;
    char buf[64];
    std::error_code ec;
    std::size_t n = 0;
    for(;;)
    {
        // std::tie assigns into ec and n; no new bindings per iteration
        std::tie(ec, n) = co_await rs.read_some(capy::make_buffer(buf));
        if(ec)
            break;
        body.append(buf, n);
    }
    // ec == capy::cond::eof when the stream ended normally
    co_return body;
}
// end::tie[]

struct results_test
{
    void
    testProduce()
    {
        capy::test::fuse f;
        auto r = f.inert([&](capy::test::fuse&) -> capy::task<> {
            auto [ec0] = co_await producing::ensure_ready(false);
            BOOST_TEST(ec0 == std::errc::not_connected);

            auto [ec1] = co_await producing::ensure_ready(true);
            BOOST_TEST(! ec1);

            auto [ec2, n] = co_await producing::count_ready(true);
            BOOST_TEST(! ec2);
            BOOST_TEST(n == 42);

            auto [ec3, n3] = co_await producing::count_ready(false);
            BOOST_TEST(ec3 == std::errc::not_connected);
            (void)n3;

            co_await producing::use_them();
        });
        BOOST_TEST(r.success);
    }

    void
    testConsume()
    {
        capy::test::fuse f;
        capy::test::read_stream rs(f);
        rs.provide("hello");
        auto r = f.inert([&](capy::test::fuse&) -> capy::task<> {
            std::size_t n = co_await read_once(rs);
            BOOST_TEST(n == 5);
        });
        BOOST_TEST(r.success);
    }

    void
    testEofCarriesPartialCount()
    {
        capy::test::fuse f;
        capy::test::read_stream rs(f);
        rs.provide("hi");
        auto r = f.inert([&](capy::test::fuse&) -> capy::task<> {
            // The claim the page makes about the `partial` fragment.
            char buf[8];
            auto [ec, n] = co_await capy::read(rs, capy::make_buffer(buf));
            BOOST_TEST(ec == capy::cond::eof);
            BOOST_TEST(n == 2);
            BOOST_TEST(std::string_view(buf, n) == "hi");
        });
        BOOST_TEST(r.success);

        capy::test::read_stream rs2(f);
        rs2.provide("hi");
        auto r2 = f.inert([&](capy::test::fuse&) -> capy::task<> {
            co_await read_header(rs2);
        });
        BOOST_TEST(r2.success);
    }

    void
    testTieRebinds()
    {
        capy::test::fuse f;
        capy::test::read_stream rs(f, 1);
        rs.provide("abc");
        auto r = f.inert([&](capy::test::fuse&) -> capy::task<> {
            std::string body = co_await read_all(rs);
            BOOST_TEST(body == "abc");

            // The condition read_all's loop stopped on.
            char buf[1];
            auto [ec, n] = co_await rs.read_some(capy::make_buffer(buf));
            BOOST_TEST(ec == capy::cond::eof);
            BOOST_TEST(n == 0);
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testProduce();
        testConsume();
        testEofCarriesPartialCount();
        testTieRebinds();
    }
};

} // namespace

TEST_SUITE(results_test, "boost.capy.doc.4k_results");
