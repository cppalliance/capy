//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/2.cpp20-coroutines/2a.foundations.adoc.
// Pages include the tagged regions; scaffolding stays outside the tags.

// Fragments deliberately leave results and bindings unused; the pages
// explain the values in prose instead.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-function"
// gcc 15 with sanitizers misattributes coroutine frame delete paths
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
#if defined(_MSC_VER)
#pragma warning(disable: 4834) // discarding [[nodiscard]] return value
#pragma warning(disable: 4189) // local variable initialized but not referenced
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4101) // unreferenced local variable
#pragma warning(disable: 4456) // declaration hides previous local declaration
#pragma warning(disable: 4457) // declaration hides function parameter
#pragma warning(disable: 4458) // declaration hides class member
#pragma warning(disable: 4459) // declaration hides global declaration
#endif

#include <boost/capy/task.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include <string>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

// tag::compute[]
int compute(int x, int y)
{
    int result = x * y + 42;
    return result;
}
// end::compute[]

// Each handle_request variant lives in its own namespace so the three
// versions shown on the page can coexist in one translation unit.

namespace sync_api {

struct parsed_request
{
    int id;
};

struct connection
{
    std::string read() { return "request"; }
    void write(std::string const&) {}
};

struct database_t
{
    std::string query(int) { return "row"; }
};

database_t database;

parsed_request parse_request(std::string const&) { return {1}; }
std::string compute_response(std::string const&) { return "response"; }

// tag::sync_request[]
void handle_request(connection& conn)
{
    std::string request = conn.read();      // blocks until data arrives
    auto parsed = parse_request(request);
    auto data = database.query(parsed.id);  // blocks until database responds
    auto response = compute_response(data);
    conn.write(response);                   // blocks until write completes
}
// end::sync_request[]

} // namespace sync_api

namespace callback_api {

struct parsed_request
{
    int id;
};

// Stubs invoke their callbacks immediately so the fragment can run
// to completion inside the test.
struct connection
{
    template<class Handler>
    void async_read(Handler h) { h(std::string("request")); }

    template<class Handler>
    void async_write(std::string const&, Handler h) { h(); }
};

struct database_t
{
    template<class Handler>
    void async_query(int, Handler h) { h(std::string("row")); }
};

database_t database;

parsed_request parse_request(std::string const&) { return {1}; }
std::string compute_response(std::string const&) { return "response"; }

// tag::callback_request[]
void handle_request(connection& conn)
{
    conn.async_read([&conn](std::string request) {
        auto parsed = parse_request(request);
        database.async_query(parsed.id, [&conn](auto data) {
            auto response = compute_response(data);
            conn.async_write(response, []() {
                // request complete
            });
        });
    });
}
// end::callback_request[]

} // namespace callback_api

namespace coro_api {

struct parsed_request
{
    int id;
};

struct connection
{
    capy::task<std::string> async_read() { co_return std::string("request"); }
    capy::task<void> async_write(std::string) { co_return; }
};

struct database_t
{
    capy::task<std::string> async_query(int) { co_return std::string("row"); }
};

database_t database;

parsed_request parse_request(std::string const&) { return {1}; }
std::string compute_response(std::string const&) { return "response"; }

using capy::task;

// tag::coroutine_request[]
task<void> handle_request(connection& conn)
{
    std::string request = co_await conn.async_read();
    auto parsed = parse_request(request);
    auto data = co_await database.async_query(parsed.id);
    auto response = compute_response(data);
    co_await conn.async_write(response);
}
// end::coroutine_request[]

} // namespace coro_api

struct foundations_test
{
    void testCompute()
    {
        BOOST_TEST(compute(2, 3) == 48);
    }

    void testSyncRequest()
    {
        sync_api::connection conn;
        sync_api::handle_request(conn);
    }

    void testCallbackRequest()
    {
        callback_api::connection conn;
        callback_api::handle_request(conn);
    }

    void testCoroutineRequest()
    {
        capy::thread_pool pool(1);
        coro_api::connection conn;
        capy::run_async(pool.get_executor())(
            coro_api::handle_request(conn));
        pool.join();
    }

    void run()
    {
        testCompute();
        testSyncRequest();
        testCallbackRequest();
        testCoroutineRequest();
    }
};

} // namespace

TEST_SUITE(foundations_test, "boost.capy.doc.2a_foundations");
