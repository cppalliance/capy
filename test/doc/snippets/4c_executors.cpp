//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4c.executors.adoc.

#include "../doc_warnings.hpp"

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

namespace capy = boost::capy;

namespace {


struct request {};
struct response {};

response process(request const&) { return {}; }

struct connection
{
    struct { int requests = 0; } stats;

    capy::task<request> read() { co_return request{}; }
    capy::task<void> write(response) { co_return; }
};

// tag::handle_client[]
capy::task<void> handle_client(connection& conn)
{
    auto req = co_await conn.read();
    auto resp = process(req);
    co_await conn.write(resp);
    conn.stats.requests++;
}
// end::handle_client[]

struct my_executor;

// tag::my_context[]
class my_context : public capy::execution_context
{
public:
    // ... custom implementation

    my_executor get_executor();
};
// end::my_context[]

// tag::shared_resource[]
class shared_resource
{
    capy::strand<capy::thread_pool::executor_type> strand_;
    int counter_ = 0;

public:
    explicit shared_resource(capy::thread_pool& pool)
        : strand_(pool.get_executor())
    {
    }

    capy::task<int> increment()
    {
        // All increments are serialized through the strand
        co_return co_await capy::run(strand_)(do_increment());
    }

private:
    capy::task<int> do_increment()
    {
        // No mutex needed—strand ensures exclusive access
        ++counter_;
        co_return counter_;
    }
};
// end::shared_resource[]

capy::task<int> independent_task(int i)
{
    co_return i;
}

struct executors_test
{
    void testHandleClient()
    {
        capy::thread_pool pool(1);
        connection conn;
        capy::run_async(pool.get_executor())(handle_client(conn));
        pool.join();
        BOOST_TEST(conn.stats.requests == 1);
    }

    void testSharedResource()
    {
        capy::thread_pool pool(2);
        shared_resource sr(pool);
        int result = 0;
        capy::run_async(pool.get_executor(), [&result](int v) {
            result = v;
        })(sr.increment());
        pool.join();
        BOOST_TEST(result == 1);
    }

    void testSingleThread()
    {
        // tag::single_thread[]
        capy::thread_pool single_thread(1);
        auto ex = single_thread.get_executor();
        // All work runs on the single thread
        // end::single_thread[]
        (void)ex;
    }

    void testDataStrand()
    {
        // tag::data_strand[]
        capy::thread_pool pool(4);
        capy::strand<capy::thread_pool::executor_type> data_strand(
            pool.get_executor());

        // Use data_strand for all access to shared data
        // Use pool.get_executor() for independent work
        // end::data_strand[]
        (void)data_strand;
    }

    void testIndependentWork()
    {
        // tag::independent_tasks[]
        capy::thread_pool pool(4);
        auto ex = pool.get_executor();

        // Start independent tasks directly on the pool
        std::vector<capy::task<int>> tasks;
        for (int i = 0; i < 100; ++i)
            capy::run_async(ex)(independent_task(i));
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
