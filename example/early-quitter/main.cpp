//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Deep call chain with quitter: a stop ends the whole chain at
// whichever co_await it is suspended in, with no checks at any level.
//
// The quitter here is example code (quitter.hpp), built only on capy's
// public coroutine protocol: no exceptions, no hooks in the library.

#include "quitter.hpp"

#include <boost/capy.hpp>

#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <iostream>
#include <latch>
#include <sstream>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <vector>

namespace capy = boost::capy;
using namespace std::chrono_literals;

static std::stop_source g_stop;

// request_stop runs the stop callbacks, which post to executors and
// allocate, so the signal handler only sets a lock-free flag and the
// ticker thread requests the stop.
static std::atomic<bool> g_signaled{false};
static_assert(std::atomic<bool>::is_always_lock_free);

extern "C" void on_signal(int) { g_signaled.store(true); }

// Logs its release, standing in for a socket, file, or buffer.
struct resource_guard
{
    std::string what;
    ~resource_guard()
    {
        std::ostringstream oss;
        oss << "  released " << what << "\n";
        std::cout << oss.str();
    }
};

std::string transform(std::string s)
{
    for(auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

early::quitter<std::string>
fetch(int id, int item, capy::async_waker& waker)
{
    resource_guard conn{"connection for item " + std::to_string(item)};

    std::ignore = co_await waker.wait(); // connect
    std::ignore = co_await waker.wait(); // read

    co_return "worker " + std::to_string(id) +
        " item " + std::to_string(item);
}

early::quitter<>
store(std::string const& data, capy::async_waker& waker)
{
    resource_guard file{"output file"};

    std::ignore = co_await waker.wait(); // open
    std::ignore = co_await waker.wait(); // write

    std::ostringstream oss;
    oss << "  stored " << data << "\n";
    std::cout << oss.str();
}

early::quitter<>
process_item(int id, int item, capy::async_waker& waker)
{
    co_await store(transform(co_await fetch(id, item, waker)), waker);
}

early::quitter<std::error_code> worker(int id, capy::async_waker& waker)
{
    resource_guard guard{"worker " + std::to_string(id)};
    for(int item = 0; ; ++item)
        co_await process_item(id, item, waker);
}

int main()
{
    std::signal(SIGINT, on_signal);

    constexpr int n = 4;
    capy::thread_pool pool(n);
    std::latch done(n);
    std::array<capy::async_waker, n> wakers;
    std::vector<capy::strand<capy::thread_pool::executor_type>> strands;
    for(int i = 0; i < n; ++i)
        strands.emplace_back(pool.get_executor());

    // Each wake stands in for one I/O operation completing.
    std::atomic<bool> ticker_stop{false};
    std::thread ticker([&] {
        while(!ticker_stop.load())
        {
            std::this_thread::sleep_for(20ms);
            if(g_signaled.load() && !g_stop.stop_requested())
                g_stop.request_stop();
            for(auto& w : wakers)
                w.wake();
        }
    });

    std::cout << "Press Ctrl+C to quit.\n";
    for(int i = 0; i < n; ++i)
        capy::run_async(strands[i], g_stop.get_token(),
            // A stop arrives here as canceled.
            [&](std::error_code) { done.count_down(); })(
                worker(i, wakers[i]));

    done.wait();
    ticker_stop = true;
    ticker.join();
    std::cout << "Shutdown complete.\n";
}
