//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <chrono>
#include <iostream>
#include <latch>
#include <thread>

using namespace boost::capy;

// A slow operation that respects cancellation
task<std::string> slow_fetch(int steps)
{
    auto token = co_await this_coro::stop_token;  // std::stop_token
    std::string result;
    
    for (int i = 0; i < steps; ++i)
    {
        // Check cancellation before each step
        if (token.stop_requested())
        {
            std::cout << "  Cancelled at step " << i << std::endl;
            throw std::system_error(
                make_error_code(std::errc::operation_canceled));
        }
        
        result += "step" + std::to_string(i) + " ";
        
        // Simulate slow work (in real code, this would be I/O)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::cout << "  Completed step " << i << std::endl;
        
        // Yield to allow stop request to be processed before next check
        // Extra 5ms ensures print completes before main thread prints
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    
    co_return result;
}

// Run with timeout (conceptual - real implementation needs timer)
task<std::optional<std::string>> fetch_with_timeout()
{
    auto token = co_await this_coro::stop_token;  // std::stop_token
    
    try
    {
        auto result = co_await slow_fetch(5);  // std::string
        co_return result;
    }
    catch (std::system_error const& e)
    {
        if (e.code() == std::errc::operation_canceled)
            co_return std::nullopt;
        throw;
    }
}

void demo_normal_completion()
{
    std::cout << "Demo: Normal completion\n";
    
    thread_pool pool;
    std::stop_source source;
    std::latch done(1);  // std::latch - wait for 1 task
    
    run_async(pool.get_executor(), source.get_token(),
        [&done](std::optional<std::string> result) {
            if (result)
                std::cout << "Result: " << *result << "\n";
            else
                std::cout << "Cancelled\n";
            done.count_down();
        },
        [&done](std::exception_ptr) { done.count_down(); }
    )(fetch_with_timeout());
    
    done.wait();  // Block until task completes
}

void demo_cancellation()
{
    std::cout << "\nDemo: Cancellation after 2 steps\n";
    
    thread_pool pool;
    std::stop_source source;
    std::latch done(1);  // std::latch - wait for 1 task
    
    // Launch the task
    run_async(pool.get_executor(), source.get_token(),
        [&done](std::optional<std::string> result) {
            if (result)
                std::cout << "Result: " << *result << "\n";
            else
                std::cout << "Cancelled (returned nullopt)\n";
            done.count_down();
        },
        [&done](std::exception_ptr) { done.count_down(); }
    )(fetch_with_timeout());
    
    // Simulate timeout: cancel after 2 steps complete
    // Timing: each step is 10ms work + 15ms yield = 25ms total
    // Step 1 prints at 35ms, step 2 check at 50ms
    // Stop at 42ms: after step 1 print, before step 2 check
    std::this_thread::sleep_for(std::chrono::milliseconds(42));
    std::cout << "  Requesting stop..." << std::endl;
    source.request_stop();
    
    done.wait();  // Block until task completes (after cancellation)
}

// Example: Manual stop token checking
task<int> process_items(std::vector<int> const& items)
{
    auto token = co_await this_coro::stop_token;  // std::stop_token
    int sum = 0;
    
    for (auto item : items)  // int
    {
        if (token.stop_requested())
        {
            std::cout << "Processing cancelled, partial sum: " << sum << "\n";
            co_return sum;  // Return partial result
        }
        
        sum += item;
    }
    
    co_return sum;
}

int main()
{
    demo_normal_completion();
    demo_cancellation();
    
    return 0;
}
