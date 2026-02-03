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

using namespace boost::capy;

int main()
{
    thread_pool pool;  // thread_pool
    strand s{pool.get_executor()};  // strand - serializes execution
    std::latch done(1);  // std::latch - wait for completion

    auto on_complete = [&done](auto&&...) { done.count_down(); };  // lambda
    auto on_error = [&done](std::exception_ptr) { done.count_down(); };  // lambda

    async_event data_ready;  // async_event
    int shared_value = 0;    // int

    auto producer = [&]() -> task<> {
        std::cout << "Producer: preparing data...\n";
        shared_value = 42;
        std::cout << "Producer: data ready, signaling\n";
        data_ready.set();
        co_return;
    };

    auto consumer = [&]() -> task<> {
        std::cout << "Consumer: waiting for data...\n";
        co_await data_ready.wait();
        std::cout << "Consumer: received value " << shared_value << "\n";
        co_return;
    };

    // Run both tasks concurrently using when_all, through a strand.
    // The strand serializes execution, ensuring thread-safe access
    // to the shared async_event and shared_value.
    auto run_both = [&]() -> task<> {
        co_await when_all(producer(), consumer());
    };

    run_async(s, on_complete, on_error)(run_both());

    done.wait();  // Block until tasks complete
    return 0;
}
