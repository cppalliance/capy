//
// Copyright (c) 2026 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/system_context.hpp>

#include "test_suite.hpp"

#include <thread>
#include <vector>

namespace boost {
namespace capy {

namespace {

struct test_service : execution_context::service
{
    int value = 42;

    explicit test_service(execution_context&) {}

    void shutdown() override {}
};

} // namespace

struct system_context_test
{
    void
    testSingleton()
    {
        // Multiple calls return the same instance
        auto& ctx1 = get_system_context();
        auto& ctx2 = get_system_context();
        BOOST_TEST_EQ(&ctx1, &ctx2);
    }

    void
    testServices()
    {
        auto& ctx = get_system_context();

        // Can add and retrieve services
        auto& svc = ctx.use_service<test_service>();
        BOOST_TEST_EQ(svc.value, 42);

        // Service persists
        BOOST_TEST(ctx.has_service<test_service>());
        BOOST_TEST_EQ(&ctx.use_service<test_service>(), &svc);
    }

    void
    testFrameAllocator()
    {
        auto& ctx = get_system_context();

        // Frame allocator is null_memory_resource (no-op)
        auto* mr = ctx.get_frame_allocator();
        BOOST_TEST_EQ(mr, std::pmr::null_memory_resource());
    }

    void
    testConcurrentAccess()
    {
        constexpr int num_threads = 4;
        std::vector<execution_context*> results(num_threads);
        std::vector<std::thread> threads;
        threads.reserve(num_threads);

        for(int i = 0; i < num_threads; ++i)
        {
            threads.emplace_back([&results, i]{
                results[i] = &get_system_context();
            });
        }

        for(auto& t : threads)
            t.join();

        // All threads got the same instance
        for(int i = 1; i < num_threads; ++i)
            BOOST_TEST_EQ(results[i], results[0]);
    }

    void
    run()
    {
        testSingleton();
        testServices();
        testFrameAllocator();
        testConcurrentAccess();
    }
};

TEST_SUITE(
    system_context_test,
    "boost.capy.system_context");

} // capy
} // boost
