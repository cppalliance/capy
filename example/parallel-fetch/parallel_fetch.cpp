//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy.hpp>
#include <iostream>
#include <latch>
#include <string>
#include <vector>

namespace capy = boost::capy;

// Simulated async operations
capy::task<int> fetch_user_id(std::string username)
{
    std::cout << "Fetching user ID for: " << username << "\n";
    // In real code: co_await http_get("/users/" + username);
    co_return static_cast<int>(username.length()) * 100;  // Fake ID
}

capy::task<std::string> fetch_user_name(int id)
{
    std::cout << "Fetching name for user ID: " << id << "\n";
    co_return "User" + std::to_string(id);
}

capy::task<int> fetch_order_count(int user_id)
{
    std::cout << "Fetching order count for user: " << user_id << "\n";
    co_return user_id / 10;  // Fake count
}

capy::task<double> fetch_account_balance(int user_id)
{
    std::cout << "Fetching balance for user: " << user_id << "\n";
    co_return user_id * 1.5;  // Fake balance
}

// Fetch all user data in parallel
capy::task<> fetch_user_dashboard(std::string username)
{
    std::cout << "\n=== Fetching dashboard for: " << username << " ===\n";
    
    // First, get the user ID (needed for other queries)
    int user_id = co_await fetch_user_id(username);
    std::cout << "Got user ID: " << user_id << "\n\n";
    
    // Fetch all user data in parallel using variadic when_all.
    // Heterogeneous return types are flattened into the result.
    std::cout << "Starting parallel fetches...\n";

    auto wrap = [](auto inner) -> capy::io_task<decltype(inner.await_resume())> {
        co_return capy::io_result<decltype(inner.await_resume())>{
            {}, co_await std::move(inner)};
    };

    auto [ec, name, orders, balance] = co_await capy::when_all(
        wrap(fetch_user_name(user_id)),
        wrap(fetch_order_count(user_id)),
        wrap(fetch_account_balance(user_id)));

    std::cout << "\nDashboard results:\n";
    std::cout << "  Name: " << name << "\n";
    std::cout << "  Orders: " << orders << "\n";
    std::cout << "  Balance: $" << balance << "\n";
}

// Example with void tasks
capy::io_task<> log_access(std::string resource)
{
    std::cout << "Logging access to: " << resource << "\n";
    co_return capy::io_result<>{};
}

capy::io_task<> update_metrics(std::string metric)
{
    std::cout << "Updating metric: " << metric << "\n";
    co_return capy::io_result<>{};
}

capy::task<std::string> fetch_with_side_effects()
{
    std::cout << "\n=== Fetch with side effects ===\n";

    auto r = co_await capy::when_all(
        log_access("api/data"),
        update_metrics("api_calls"));
    if (r.ec)
        co_return "error";

    auto data = co_await fetch_user_name(42);

    std::cout << "Data: " << data << "\n";
    co_return data;
}

// Error handling example
capy::io_task<int> might_fail(bool should_fail, std::string name)
{
    std::cout << "Task " << name << " starting\n";

    if (should_fail)
    {
        throw std::runtime_error(name + " failed!");
    }

    std::cout << "Task " << name << " completed\n";
    co_return capy::io_result<int>{{}, 42};
}

capy::task<> demonstrate_error_handling()
{
    std::cout << "\n=== Error handling ===\n";
    
    try
    {
        auto [ec2, a, b, c] = co_await capy::when_all(
            might_fail(false, "A"),
            might_fail(true, "B"),   // This one fails
            might_fail(false, "C"));
        std::cout << "All succeeded: " << a << ", "
                  << b << ", " << c << "\n";
    }
    catch (std::runtime_error const& e)
    {
        std::cout << "Caught error: " << e.what() << "\n";
        // Note: when_all waits for all tasks to complete (or respond to stop)
        // before propagating the first exception
    }
}

int main()
{
    capy::thread_pool pool;
    std::latch done(3);  // std::latch - wait for 3 tasks

    // Completion handlers signal the latch when each task finishes
    // Use generic lambda to accept any result type (or no result for task<void>)
    auto on_complete = [&done](auto&&...) { done.count_down(); };
    auto on_error = [&done](std::exception_ptr) { done.count_down(); };

    capy::run_async(pool.get_executor(), on_complete, on_error)(fetch_user_dashboard("alice"));
    capy::run_async(pool.get_executor(), on_complete, on_error)(fetch_with_side_effects());
    capy::run_async(pool.get_executor(), on_complete, on_error)(demonstrate_error_handling());
    
    done.wait();  // Block until all tasks complete
    return 0;
}
