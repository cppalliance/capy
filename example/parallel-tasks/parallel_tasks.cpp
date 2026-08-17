//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Parallel Tasks Example
//
// Distributes CPU-bound work across a thread_pool and collects
// results with when_all.  Each task sums a range of integers
// and prints its thread ID to show parallel execution.
//

#include <boost/capy.hpp>
#include <iostream>
#include <latch>
#include <sstream>
#include <thread>
#include <vector>

namespace capy = boost::capy;

// Sum integers in [lo, hi)
capy::io_task<long long> partial_sum(int lo, int hi)
{
    std::ostringstream oss;
    oss << "  range [" << lo << ", " << hi
        << ") on thread " << std::this_thread::get_id() << "\n";
    std::cout << oss.str();

    long long sum = 0;
    for (int i = lo; i < hi; ++i)
        sum += i;
    co_return capy::io_result<long long>{std::error_code(), sum};
}

int main()
{
    constexpr int total = 10000;
    constexpr int num_tasks = 4;
    constexpr int chunk = total / num_tasks;

    capy::thread_pool pool(num_tasks);
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

    auto compute = [&]() -> capy::task<> {
        std::cout << "Dispatching " << num_tasks
                  << " parallel tasks...\n";

        std::vector<capy::io_task<long long>> tasks;
        for (int i = 0; i < num_tasks; ++i)
            tasks.push_back(partial_sum(i * chunk, (i + 1) * chunk));

        auto [ec, sums] = co_await capy::when_all(std::move(tasks));

        long long total_sum = 0;
        for (auto s : sums)
            total_sum += s;

        // Arithmetic series: sum [0, N) = N*(N-1)/2
        long long expected =
            static_cast<long long>(total) * (total - 1) / 2;

        std::cout << "\nPartial sums:";
        for (std::size_t i = 0; i < sums.size(); ++i)
        {
            if (i > 0) std::cout << " +";
            std::cout << " " << sums[i];
        }
        std::cout << "\nTotal: " << total_sum
                  << " (expected " << expected << ")\n";
    };

    capy::run_async(pool.get_executor(), on_complete, on_error)(compute());
    done.wait();

    return 0;
}
