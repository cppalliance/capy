//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Producer-Consumer Example
//
// Demonstrates coordination between coroutines using async_event
// for signaling when data is ready, with strand for serialization.
//

#include <boost/capy.hpp>
#include <boost/capy/ex/strand.hpp>
#include <iostream>
#include <latch>

namespace capy = boost::capy;

int main()
{
    capy::thread_pool pool;
    capy::strand s{pool.get_executor()};
    std::latch done(1);

    auto on_complete = [&done](auto&&...) { done.count_down(); };
    auto on_error = [&done](std::exception_ptr) { done.count_down(); };

    capy::async_event data_ready;
    int shared_value = 0;

    auto producer = [&]() -> capy::io_task<> {
        std::cout << "Producer: preparing data...\n";
        shared_value = 42;
        std::cout << "Producer: data ready, signaling\n";
        data_ready.set();
        co_return capy::io_result<>{};
    };

    auto consumer = [&]() -> capy::io_task<> {
        std::cout << "Consumer: waiting for data...\n";
        auto [ec] = co_await data_ready.wait();
        (void)ec;
        std::cout << "Consumer: received value " << shared_value << "\n";
        co_return capy::io_result<>{};
    };

    // Run both tasks concurrently using when_all, through a strand.
    // The strand serializes execution, ensuring thread-safe access
    // to the shared async_event and shared_value.
    auto run_both = [&]() -> capy::task<> {
        (void) co_await capy::when_all(producer(), consumer());
    };

    capy::run_async(s, on_complete, on_error)(run_both());

    done.wait();  // Block until tasks complete
    return 0;
}
