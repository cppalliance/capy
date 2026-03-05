//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Async Mutex Example
//
// Demonstrates async_mutex for fair FIFO coroutine locking.
// Multiple worker coroutines compete for a shared resource;
// the mutex ensures mutual exclusion and FIFO acquisition order.
//

#include <boost/capy.hpp>
#include <iostream>
#include <latch>
#include <vector>

namespace capy = boost::capy;

int main()
{
    capy::thread_pool pool;
    capy::strand s{pool.get_executor()};
    std::latch done(1);

    auto on_complete = [&done](auto&&...) { done.count_down(); };
    auto on_error = [&done](std::exception_ptr ep) {
        try { std::rethrow_exception(ep); }
        catch (std::exception const& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
        catch (...) {
            std::cerr << "Error: unknown exception\n";
        }
        done.count_down();
    };

    capy::async_mutex mtx;
    int acquisition_order = 0;
    std::vector<int> order_log;

    auto worker = [&](int id) -> capy::task<> {
        std::cout << "Worker " << id << " waiting for lock\n";
        auto [ec, guard] = co_await mtx.scoped_lock();
        if (ec)
        {
            std::cout << "Worker " << id
                      << " canceled: " << ec.message() << "\n";
            co_return;
        }

        int seq = acquisition_order++;
        order_log.push_back(id);
        std::cout << "Worker " << id
                  << " acquired lock (sequence " << seq << ")\n";

        // Simulate work inside the critical section
        std::cout << "Worker " << id << " releasing lock\n";
        co_return;
    };

    auto run_all = [&]() -> capy::task<> {
        co_await capy::when_all(
            worker(0), worker(1), worker(2),
            worker(3), worker(4), worker(5));
    };

    // Run on a strand so async_mutex operations are single-threaded
    capy::run_async(s, on_complete, on_error)(run_all());
    done.wait();

    std::cout << "\nAcquisition order: ";
    for (std::size_t i = 0; i < order_log.size(); ++i)
    {
        if (i > 0)
            std::cout << " -> ";
        std::cout << "W" << order_log[i];
    }
    std::cout << "\n";

    return 0;
}
