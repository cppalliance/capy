//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

/*
   when_any cancellation example

   Demonstrates how when_any automatically cancels sibling tasks
   after the first task completes. The key points:

   1. when_any races N tasks and returns when the first completes
   2. It immediately requests stop for all remaining siblings
   3. Tasks must cooperatively check their stop token to exit early
   4. All tasks finish (cancelled or not) before when_any returns

   The example simulates three data sources with different latencies.
   The fastest source wins, and the slower ones observe the stop
   signal and bail out early.
*/

#include <boost/capy.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <chrono>
#include <iostream>
#include <latch>
#include <string>
#include <thread>
#include <vector>

namespace capy = boost::capy;

// Simulates a data source that takes `steps` iterations to produce a result.
// Each step checks the stop token so the task exits promptly when cancelled.
capy::io_task<std::string> fetch_from_source(
    std::string name, int steps, int step_ms)
{
    auto token = co_await capy::this_coro::stop_token;

    for (int i = 0; i < steps; ++i)
    {
        if (token.stop_requested())
        {
            std::cout << "  [" << name << "] cancelled at step "
                      << i << "/" << steps << "\n";
            co_return capy::io_result<std::string>{{}, name + ": cancelled"};
        }

        // Simulate work
        std::this_thread::sleep_for(
            std::chrono::milliseconds(step_ms));

        std::cout << "  [" << name << "] completed step "
                  << (i + 1) << "/" << steps << "\n";
    }

    co_return capy::io_result<std::string>{{}, name + ": done"};
}

// Race three sources — the fastest one wins, the rest get cancelled.
capy::task<> race_data_sources()
{
    std::cout << "=== Racing three data sources ===\n\n";

    // source_a: 2 steps * 20ms = fast
    // source_b: 5 steps * 20ms = medium
    // source_c: 8 steps * 20ms = slow
    auto result = co_await capy::when_any(
        fetch_from_source("source_a", 2, 20),
        fetch_from_source("source_b", 5, 20),
        fetch_from_source("source_c", 8, 20));

    if (result.index() != 0)
    {
        std::visit([](auto const& v) {
            if constexpr (!std::is_same_v<
                std::decay_t<decltype(v)>, std::error_code>)
                std::cout << "\nWinner: \"" << v << "\"\n";
        }, result);
    }
}

// A void task that loops until stopped.
// Useful for background workers that run indefinitely.
capy::io_task<> background_worker(std::string name, int step_ms)
{
    auto token = co_await capy::this_coro::stop_token;
    int iteration = 0;

    while (!token.stop_requested())
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(step_ms));
        ++iteration;
        std::cout << "  [" << name << "] tick " << iteration << "\n";
    }

    std::cout << "  [" << name << "] stopped after "
              << iteration << " iterations\n";
    co_return capy::io_result<>{};
}

// A task that finishes after a fixed delay (acts as a timeout).
capy::io_task<> timeout(int ms)
{
    std::this_thread::sleep_for(
        std::chrono::milliseconds(ms));
    std::cout << "  [timeout] expired after " << ms << "ms\n";
    co_return capy::io_result<>{};
}

// Race three replicas using variadic overload.
capy::task<> race_vector_of_sources()
{
    std::cout << "\n=== Racing three replicas ===\n\n";

    auto result = co_await capy::when_any(
        fetch_from_source("replica_1", 6, 20),
        fetch_from_source("replica_2", 3, 20),
        fetch_from_source("replica_3", 5, 20));

    if (result.index() != 0)
    {
        std::visit([](auto const& v) {
            if constexpr (!std::is_same_v<
                std::decay_t<decltype(v)>, std::error_code>)
                std::cout << "\nFastest replica: \"" << v << "\"\n";
        }, result);
    }
}

// Run all demos sequentially so output is readable.
capy::task<> run_demos()
{
    co_await race_data_sources();
    co_await race_vector_of_sources();
}

int main()
{
    capy::thread_pool pool;
    std::latch done(1);

    capy::run_async(pool.get_executor(),
        [&done](auto&&...) { done.count_down(); },
        [&done](std::exception_ptr ep) {
            try { std::rethrow_exception(ep); }
            catch (std::exception const& e) {
                std::cerr << "Error: " << e.what() << "\n";
            }
            done.count_down();
        }
    )(run_demos());

    done.wait();
    return 0;
}
