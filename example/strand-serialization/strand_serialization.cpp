//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Strand Serialization Example
//
// Demonstrates protecting shared state with a strand instead of a mutex.
// Multiple coroutines increment a shared counter concurrently on a
// multi-threaded thread_pool; the strand guarantees serialized access.
//

#include <boost/capy.hpp>
#include <iostream>
#include <latch>

namespace capy = boost::capy;

int main()
{
    constexpr int num_coroutines = 10;
    constexpr int increments_per_coro = 1000;

    capy::thread_pool pool(4);
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

    int counter = 0;

    // Each coroutine increments the shared counter without locks.
    // The strand ensures only one coroutine runs at a time.
    auto increment = [&](int id) -> capy::task<> {
        for (int i = 0; i < increments_per_coro; ++i)
            ++counter;
        std::cout << "Coroutine " << id
                  << " finished, counter = " << counter << "\n";
        co_return;
    };

    auto run_all = [&]() -> capy::task<> {
        co_await capy::when_all(
            increment(0), increment(1), increment(2),
            increment(3), increment(4), increment(5),
            increment(6), increment(7), increment(8),
            increment(9));
    };

    capy::run_async(s, on_complete, on_error)(run_all());
    done.wait();

    int expected = num_coroutines * increments_per_coro;
    std::cout << "\nFinal counter: " << counter
              << " (expected " << expected << ")\n";

    return 0;
}
