//
// Copyright (c) 2026 Michael Vandeberg
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

/* Quitter Shutdown Example

   Demonstrates quitter<T> for responsive application shutdown.

   Four workers simulate a batch file-processing pipeline: each
   "downloads" data, "transforms" it, and "writes" the result.
   A single "ticker" thread plays the role of the clock: it wakes
   each worker's async_waker on an interval, and the worker's
   co_await waker.wait() is the suspension point.  Workers are
   quitter<> coroutines:
   their bodies contain zero cancellation-handling code.

   Press Ctrl+C to request shutdown.  Every in-flight worker
   exits at its next co_await, RAII cleanup runs (each worker
   holds a resource_guard that logs its cleanup), and the
   application prints a summary and exits.

   Contrast with task<>:
     With task<>, every co_await that touches I/O needs:
       auto [ec] = co_await waker.wait();
       if(ec) co_return;            // <-- cancellation boilerplate
     This is repeated at every suspension point.

     With quitter<>, the promise intercepts the stop token
     automatically.  The worker body is pure business logic.
*/

#include <boost/capy.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <latch>
#include <sstream>
#include <stop_token>
#include <thread>
#include <vector>

namespace capy = boost::capy;
using namespace std::chrono_literals;

// Global stop source wired to Ctrl+C.
static std::stop_source g_stop;
static std::atomic<std::chrono::steady_clock::time_point>
    g_stop_time{std::chrono::steady_clock::time_point{}};

extern "C" void signal_handler(int)
{
    g_stop_time.store(std::chrono::steady_clock::now(),
        std::memory_order_relaxed);
    g_stop.request_stop();
}

// RAII resource that logs construction and destruction.
// Simulates holding a file handle, socket, or temp buffer
// that must be released on shutdown.
struct resource_guard
{
    int id;
    std::atomic<int>& cleanup_count;

    resource_guard(int id_, std::atomic<int>& count)
        : id(id_)
        , cleanup_count(count)
    {
        std::ostringstream oss;
        oss << "  [worker " << id << "] acquired resources\n";
        std::cout << oss.str();
    }

    ~resource_guard()
    {
        ++cleanup_count;
        std::ostringstream oss;
        oss << "  [worker " << id << "] released resources "
            << "(cleanup)\n";
        std::cout << oss.str();
    }

    resource_guard(resource_guard const&) = delete;
    resource_guard& operator=(resource_guard const&) = delete;
};

// A single worker: download → transform → write, repeated.
// No cancellation code.  quitter handles it.
capy::quitter<> worker(
    int id,
    capy::async_waker& waker,
    std::atomic<int>& items_processed,
    std::atomic<int>& cleanup_count)
{
    resource_guard guard(id, cleanup_count);

    for(int item = 0; ; ++item)
    {
        // Simulate download: suspend until the ticker wakes us.
        (void) co_await waker.wait();

        // Simulate transform (CPU work — no co_await needed)
        {
            std::ostringstream oss;
            oss << "  [worker " << id << "] processing item "
                << item << "\n";
            std::cout << oss.str();
        }

        // Simulate write: suspend for another wakeup.
        (void) co_await waker.wait();

        ++items_processed;
    }

    // Never reached — the loop is infinite.
    // quitter exits at the next co_await after stop is requested.
}

int main()
{
    std::signal(SIGINT, signal_handler);
#ifdef SIGTERM
    std::signal(SIGTERM, signal_handler);
#endif

    constexpr int num_workers = 4;
    capy::thread_pool pool(num_workers);
    std::latch done(num_workers);

    std::atomic<int> items_processed{0};
    std::atomic<int> cleanup_count{0};

    // One waker per worker (single-waiter precondition); each
    // runs on its own strand so the pool's num_workers OS threads
    // still keep every waker's resumption single-threaded.
    std::array<capy::async_waker, num_workers> wakers;
    std::vector<capy::strand<capy::thread_pool::executor_type>> strands;
    strands.reserve(num_workers);
    for(int i = 0; i < num_workers; ++i)
        strands.emplace_back(pool.get_executor());

    // Ticker thread paces the workers: it periodically wakes
    // every worker's waker.
    std::atomic<bool> ticker_stop{false};
    std::thread ticker([&] {
        while(!ticker_stop.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(50ms);
            for(auto& waker : wakers)
                waker.wake();
        }
    });

    std::cout << "Starting " << num_workers
              << " workers.  Press Ctrl+C to quit.\n\n";

    for(int i = 0; i < num_workers; ++i)
    {
        capy::run_async(
            strands[i],
            g_stop.get_token(),
            [&]() { done.count_down(); },
            [&](std::exception_ptr) { done.count_down(); })(
                worker(i, wakers[i], items_processed, cleanup_count));
    }

    done.wait();

    // Stop and join the ticker now that the pool has drained, so
    // it cannot wake a waker (or post to a strand) after the
    // pool starts tearing down.
    ticker_stop.store(true, std::memory_order_relaxed);
    ticker.join();

    auto stop_at = g_stop_time.load(std::memory_order_relaxed);
    auto now = std::chrono::steady_clock::now();

    std::cout << "\nShutdown complete.\n"
              << "  Items processed: " << items_processed << "\n"
              << "  Workers cleaned up: " << cleanup_count
              << "/" << num_workers << "\n";

    if(stop_at != std::chrono::steady_clock::time_point{})
    {
        auto us = std::chrono::duration_cast<
            std::chrono::microseconds>(now - stop_at).count();
        std::cout << "  Shutdown latency: " << us << " us\n";
    }
}
