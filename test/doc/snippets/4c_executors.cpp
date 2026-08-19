//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4c.executors.adoc.

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

#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
// tag::shared_resource[]
#include <boost/capy/ex/strand.hpp>

// end::shared_resource[]
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>

#include <vector>

#include "test_suite.hpp"

namespace {

using namespace boost::capy;

struct request {};
struct response {};

response process(request const&) { return {}; }

struct connection
{
    struct { int requests = 0; } stats;

    task<request> read() { co_return request{}; }
    task<void> write(response) { co_return; }
};

// tag::handle_client[]
task<void> handle_client(connection& conn)
{
    auto req = co_await conn.read();
    auto resp = process(req);
    co_await conn.write(resp);
    conn.stats.requests++;
}
// end::handle_client[]

struct my_executor;

// tag::my_context[]
class my_context : public execution_context
{
public:
    // ... custom implementation

    my_executor get_executor();
};
// end::my_context[]

// tag::shared_resource[]
class shared_resource
{
    strand<thread_pool::executor_type> strand_;
    int counter_ = 0;

public:
    explicit shared_resource(thread_pool& pool)
        : strand_(pool.get_executor())
    {
    }

    task<int> increment()
    {
        // All increments are serialized through the strand
        co_return co_await run(strand_)(do_increment());
    }

private:
    task<int> do_increment()
    {
        // No mutex needed—strand ensures exclusive access
        ++counter_;
        co_return counter_;
    }
};
// end::shared_resource[]

task<int> independent_task(int i)
{
    co_return i;
}

struct executors_test
{
    void testHandleClient()
    {
        thread_pool pool(1);
        connection conn;
        run_async(pool.get_executor())(handle_client(conn));
        pool.join();
        BOOST_TEST(conn.stats.requests == 1);
    }

    void testSharedResource()
    {
        thread_pool pool(2);
        shared_resource sr(pool);
        int result = 0;
        run_async(pool.get_executor(), [&result](int v) {
            result = v;
        })(sr.increment());
        pool.join();
        BOOST_TEST(result == 1);
    }

    void testSingleThread()
    {
        // tag::single_thread[]
        thread_pool single_thread(1);
        auto ex = single_thread.get_executor();
        // All work runs on the single thread
        // end::single_thread[]
        (void)ex;
    }

    void testDataStrand()
    {
        // tag::data_strand[]
        thread_pool pool(4);
        strand<thread_pool::executor_type> data_strand(pool.get_executor());

        // Use data_strand for all access to shared data
        // Use pool.get_executor() for independent work
        // end::data_strand[]
        (void)data_strand;
    }

    void testIndependentWork()
    {
        // tag::independent_tasks[]
        thread_pool pool(4);
        auto ex = pool.get_executor();

        // Start independent tasks directly on the pool
        std::vector<task<int>> tasks;
        for (int i = 0; i < 100; ++i)
            run_async(ex)(independent_task(i));
        // end::independent_tasks[]
        pool.join();
    }

    void run()
    {
        testHandleClient();
        testSharedResource();
        testSingleThread();
        testDataStrand();
        testIndependentWork();
    }
};

} // namespace

TEST_SUITE(executors_test, "boost.capy.doc.4c_executors");
