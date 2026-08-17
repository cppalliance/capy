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

// tag::full[]
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

    // tag::mutex[]
    capy::async_mutex mtx;
    // end::mutex[]
    int acquisition_order = 0;
    std::vector<int> order_log;

    auto worker = [&](int id) -> capy::io_task<> {
        std::cout << "Worker " << id << " waiting for lock\n";
        // tag::scoped_lock[]
        auto [ec, guard] = co_await mtx.scoped_lock();
        if (ec)
        {
            std::cout << "Worker " << id
                      << " canceled: " << ec.message() << "\n";
            co_return capy::io_result<>{ec};
        }
        // end::scoped_lock[]

        int seq = acquisition_order++;
        order_log.push_back(id);
        std::cout << "Worker " << id
                  << " acquired lock (sequence " << seq << ")\n";

        std::cout << "Worker " << id << " releasing lock\n";
        co_return capy::io_result<>{};
    };

    auto run_all = [&]() -> capy::task<> {
        auto r = co_await capy::when_all(
            worker(0), worker(1), worker(2),
            worker(3), worker(4), worker(5));
        if(std::get<0>(r))
            std::cerr << "when_all error: "
                      << std::get<0>(r).message() << "\n";
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
// end::full[]
