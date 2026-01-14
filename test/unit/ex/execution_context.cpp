//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/ex/execution_context.hpp>

#include "test_suite.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace boost {
namespace capy {

namespace {

// Test execution context that exposes protected constructor
class test_context : public execution_context
{
public:
    test_context() = default;
};

// Simple service for basic tests
struct simple_service : execution_context::service
{
    int value = 0;

    explicit simple_service(execution_context&)
    {
    }

    simple_service(execution_context&, int v)
        : value(v)
    {
    }

    void shutdown() override {}
};

// Service that tracks shutdown
struct tracking_service : execution_context::service
{
    bool& shutdown_called;

    explicit tracking_service(execution_context&, bool& flag)
        : shutdown_called(flag)
    {
    }

    void shutdown() override
    {
        shutdown_called = true;
    }
};

// Base service type for key_type tests
struct base_service : execution_context::service
{
    virtual int get_value() const = 0;
};

// Derived service with key_type
struct derived_service : base_service
{
    using key_type = base_service;

    int value;

    explicit derived_service(execution_context&)
        : value(42)
    {
    }

    derived_service(execution_context&, int v)
        : value(v)
    {
    }

    int get_value() const override { return value; }
    void shutdown() override {}
};

// Another derived service with same key_type
struct other_derived_service : base_service
{
    using key_type = base_service;

    explicit other_derived_service(execution_context&)
    {
    }

    int get_value() const override { return 99; }
    void shutdown() override {}
};

// Service with multiple constructor args
struct multi_arg_service : execution_context::service
{
    int a;
    std::string b;
    double c;

    multi_arg_service(execution_context&, int a_, std::string b_, double c_)
        : a(a_), b(std::move(b_)), c(c_)
    {
    }

    void shutdown() override {}
};

// Service that creates another service in constructor
struct nested_service : execution_context::service
{
    explicit nested_service(execution_context& ctx)
    {
        // This should work - nested service creation
        ctx.use_service<simple_service>();
    }

    void shutdown() override {}
};

// Concrete handler for testing data() behavior
struct test_handler : execution_context::handler
{
    void operator()() override {}
    void destroy() override {}

    // Expose data_ for testing
    void set_data(void* p) { data_ = p; }
};

} // namespace

struct execution_context_test
{
    void
    testConstruct()
    {
        // Basic construction and destruction
        {
            test_context ctx;
        }
    }

    void
    testHasService()
    {
        test_context ctx;

        // Initially no services
        BOOST_TEST(!ctx.has_service<simple_service>());

        // After adding, has_service returns true
        ctx.use_service<simple_service>();
        BOOST_TEST(ctx.has_service<simple_service>());
    }

    void
    testFindService()
    {
        test_context ctx;

        // Initially returns nullptr
        BOOST_TEST_EQ(ctx.find_service<simple_service>(), nullptr);

        // After adding, returns pointer
        ctx.use_service<simple_service>();
        BOOST_TEST_NE(ctx.find_service<simple_service>(), nullptr);
    }

    void
    testUseService()
    {
        test_context ctx;

        // Creates service if not present
        auto& svc1 = ctx.use_service<simple_service>();
        BOOST_TEST(ctx.has_service<simple_service>());

        // Returns same instance on subsequent calls
        auto& svc2 = ctx.use_service<simple_service>();
        BOOST_TEST_EQ(&svc1, &svc2);
    }

    void
    testMakeService()
    {
        test_context ctx;

        // Creates service with value
        auto& svc = ctx.make_service<simple_service>(42);
        BOOST_TEST_EQ(svc.value, 42);

        // Throws if service already exists
        BOOST_TEST_THROWS(
            ctx.make_service<simple_service>(100),
            std::invalid_argument);

        // Original value unchanged
        BOOST_TEST_EQ(ctx.find_service<simple_service>()->value, 42);
    }

    void
    testMakeServiceMultipleArgs()
    {
        test_context ctx;

        auto& svc = ctx.make_service<multi_arg_service>(
            123, std::string("hello"), 3.14);

        BOOST_TEST_EQ(svc.a, 123);
        BOOST_TEST_EQ(svc.b, "hello");
        BOOST_TEST_EQ(svc.c, 3.14);
    }

    void
    testKeyType()
    {
        test_context ctx;

        // Create derived service
        auto& svc = ctx.make_service<derived_service>(100);
        BOOST_TEST_EQ(svc.value, 100);

        // Can find via derived type
        BOOST_TEST(ctx.has_service<derived_service>());
        BOOST_TEST_NE(ctx.find_service<derived_service>(), nullptr);

        // Can find via base type (key_type)
        BOOST_TEST(ctx.has_service<base_service>());
        auto* base = ctx.find_service<base_service>();
        BOOST_TEST_NE(base, nullptr);
        BOOST_TEST_EQ(base->get_value(), 100);

        // Cannot add another service with same key_type
        BOOST_TEST_THROWS(
            ctx.make_service<other_derived_service>(),
            std::invalid_argument);
    }

    void
    testKeyTypeUseService()
    {
        test_context ctx;

        // use_service creates via derived type
        auto& svc = ctx.use_service<derived_service>();
        BOOST_TEST_EQ(svc.get_value(), 42);

        // Can lookup via base type
        BOOST_TEST(ctx.has_service<base_service>());
    }

    void
    testShutdown()
    {
        bool shutdown_called = false;

        {
            test_context ctx;
            ctx.make_service<tracking_service>(shutdown_called);
            BOOST_TEST(!shutdown_called);
        }

        // Shutdown called when context destroyed
        BOOST_TEST(shutdown_called);
    }

    void
    testMultipleServices()
    {
        test_context ctx;

        ctx.make_service<simple_service>(1);
        ctx.make_service<multi_arg_service>(2, "test", 2.0);

        BOOST_TEST(ctx.has_service<simple_service>());
        BOOST_TEST(ctx.has_service<multi_arg_service>());

        BOOST_TEST_EQ(ctx.find_service<simple_service>()->value, 1);
        BOOST_TEST_EQ(ctx.find_service<multi_arg_service>()->a, 2);
    }

    void
    testNestedServiceCreation()
    {
        test_context ctx;

        // nested_service creates simple_service in its constructor
        ctx.use_service<nested_service>();

        // Both services should exist
        BOOST_TEST(ctx.has_service<nested_service>());
        BOOST_TEST(ctx.has_service<simple_service>());
    }

    void
    testConcurrentAccess()
    {
        test_context ctx;
        std::atomic<int> success_count{0};
        constexpr int num_threads = 8;

        std::vector<std::thread> threads;
        threads.reserve(num_threads);

        for(int i = 0; i < num_threads; ++i)
        {
            threads.emplace_back([&ctx, &success_count]{
                // All threads try to use_service simultaneously
                auto& svc = ctx.use_service<simple_service>();
                (void)svc;
                ++success_count;
            });
        }

        for(auto& t : threads)
            t.join();

        // All threads should succeed
        BOOST_TEST_EQ(success_count.load(), num_threads);

        // Only one service instance should exist
        BOOST_TEST(ctx.has_service<simple_service>());
    }

    void
    testHandlerDataInitiallyNull()
    {
        test_handler h;
        BOOST_TEST_EQ(h.data(), nullptr);
    }

    void
    testHandlerDataReflectsChanges()
    {
        test_handler h;
        int dummy = 42;

        h.set_data(&dummy);
        BOOST_TEST_EQ(h.data(), &dummy);

        // Change to different value
        double other = 3.14;
        h.set_data(&other);
        BOOST_TEST_EQ(h.data(), &other);

        // Change back to nullptr
        h.set_data(nullptr);
        BOOST_TEST_EQ(h.data(), nullptr);
    }

    void
    run()
    {
        testConstruct();
        testHasService();
        testFindService();
        testUseService();
        testMakeService();
        testMakeServiceMultipleArgs();
        testKeyType();
        testKeyTypeUseService();
        testShutdown();
        testMultipleServices();
        testNestedServiceCreation();
        testConcurrentAccess();
        testHandlerDataInitiallyNull();
        testHandlerDataReflectsChanges();
    }
};

TEST_SUITE(
    execution_context_test,
    "boost.capy.execution_context");

} // capy
} // boost
