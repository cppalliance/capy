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

using namespace boost::capy;

// Simulated async operations
task<int> fetch_user_id(std::string username)
{
    std::cout << "Fetching user ID for: " << username << "\n";
    // In real code: co_await http_get("/users/" + username);
    co_return static_cast<int>(username.length()) * 100;  // Fake ID
}

task<std::string> fetch_user_name(int id)
{
    std::cout << "Fetching name for user ID: " << id << "\n";
    co_return "User" + std::to_string(id);
}

task<int> fetch_order_count(int user_id)
{
    std::cout << "Fetching order count for user: " << user_id << "\n";
    co_return user_id / 10;  // Fake count
}

task<double> fetch_account_balance(int user_id)
{
    std::cout << "Fetching balance for user: " << user_id << "\n";
    co_return user_id * 1.5;  // Fake balance
}

// Fetch all user data in parallel
task<> fetch_user_dashboard(std::string username)
{
    std::cout << "\n=== Fetching dashboard for: " << username << " ===\n";
    
    // First, get the user ID (needed for other queries)
    int user_id = co_await fetch_user_id(username);
    std::cout << "Got user ID: " << user_id << "\n\n";
    
    // Now fetch all user data in parallel
    std::cout << "Starting parallel fetches...\n";
    // name: std::string, orders: int, balance: double
    auto [name, orders, balance] = co_await when_all(
        fetch_user_name(user_id),
        fetch_order_count(user_id),
        fetch_account_balance(user_id)
    );
    
    std::cout << "\nDashboard results:\n";
    std::cout << "  Name: " << name << "\n";
    std::cout << "  Orders: " << orders << "\n";
    std::cout << "  Balance: $" << balance << "\n";
}

// Example with void tasks
task<> log_access(std::string resource)
{
    std::cout << "Logging access to: " << resource << "\n";
    co_return;
}

task<> update_metrics(std::string metric)
{
    std::cout << "Updating metric: " << metric << "\n";
    co_return;
}

task<std::string> fetch_with_side_effects()
{
    std::cout << "\n=== Fetch with side effects ===\n";
    
    // void tasks don't contribute to result tuple
    std::tuple<std::string> results = co_await when_all(
        log_access("api/data"),           // void - no result
        update_metrics("api_calls"),      // void - no result
        fetch_user_name(42)               // returns string
    );
    std::string data = std::get<0>(results);  // std::string
    
    std::cout << "Data: " << data << "\n";
    co_return data;
}

// Error handling example
task<int> might_fail(bool should_fail, std::string name)
{
    std::cout << "Task " << name << " starting\n";
    
    if (should_fail)
    {
        throw std::runtime_error(name + " failed!");
    }
    
    std::cout << "Task " << name << " completed\n";
    co_return 42;
}

task<> demonstrate_error_handling()
{
    std::cout << "\n=== Error handling ===\n";
    
    try
    {
        // a: int, b: int, c: int
        auto [a, b, c] = co_await when_all(
            might_fail(false, "A"),
            might_fail(true, "B"),   // This one fails
            might_fail(false, "C")
        );
        std::cout << "All succeeded: " << a << ", " << b << ", " << c << "\n";
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
    thread_pool pool;
    std::latch done(3);  // std::latch - wait for 3 tasks
    
    // Completion handlers signal the latch when each task finishes
    // Use generic lambda to accept any result type (or no result for task<void>)
    auto on_complete = [&done](auto&&...) { done.count_down(); };
    auto on_error = [&done](std::exception_ptr) { done.count_down(); };
    
    run_async(pool.get_executor(), on_complete, on_error)(fetch_user_dashboard("alice"));
    run_async(pool.get_executor(), on_complete, on_error)(fetch_with_side_effects());
    run_async(pool.get_executor(), on_complete, on_error)(demonstrate_error_handling());
    
    done.wait();  // Block until all tasks complete
    return 0;
}
