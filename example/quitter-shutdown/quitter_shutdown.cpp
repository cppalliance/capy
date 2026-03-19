//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

/* Quitter Shutdown Example

   Demonstrates quitter<T> for responsive application shutdown.

   Four workers simulate a batch file-processing pipeline: each
   "downloads" data (delay), "transforms" it, and "writes" the
   result (delay).  Workers are quitter<> coroutines — their
   bodies contain zero cancellation-handling code.

   Press Ctrl+C to request shutdown.  Every in-flight worker
   exits at its next co_await, RAII cleanup runs (each worker
   holds a resource_guard that logs its cleanup), and the
   application prints a summary and exits.

   Contrast with task<>:
     With task<>, every co_await that touches I/O needs:
       auto [ec] = co_await delay(dur);
       if(ec) co_return;            // <-- cancellation boilerplate
     This is repeated at every suspension point.

     With quitter<>, the promise intercepts the stop token
     automatically.  The worker body is pure business logic.
*/

#include <boost/capy.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <latch>
#include <sstream>
#include <stop_token>

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
    std::atomic<int>& items_processed,
    std::atomic<int>& cleanup_count)
{
    resource_guard guard(id, cleanup_count);

    for(int item = 0; ; ++item)
    {
        // Simulate download (200-400ms depending on worker)
        auto download_time = 200ms + 50ms * id;
        (void) co_await capy::delay(download_time);

        // Simulate transform (CPU work — no co_await needed)
        {
            std::ostringstream oss;
            oss << "  [worker " << id << "] processing item "
                << item << "\n";
            std::cout << oss.str();
        }

        // Simulate write (100ms)
        (void) co_await capy::delay(100ms);

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

    std::cout << "Starting " << num_workers
              << " workers.  Press Ctrl+C to quit.\n\n";

    for(int i = 0; i < num_workers; ++i)
    {
        capy::run_async(
            pool.get_executor(),
            g_stop.get_token(),
            [&]() { done.count_down(); },
            [&](std::exception_ptr) { done.count_down(); })(
                worker(i, items_processed, cleanup_count));
    }

    done.wait();

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
