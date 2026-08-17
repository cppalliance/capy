//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/8.examples/8m.parallel-tasks.adoc. Uses the
// variadic when_all overload; example/parallel-tasks uses the range overload.

// tag::full[]
#include <boost/capy.hpp>
#include <iostream>
#include <latch>
#include <sstream>
#include <thread>

namespace capy = boost::capy;

// Sum integers in [lo, hi)
capy::io_task<long long> partial_sum(int lo, int hi)
{
    // tag::thread_id[]
    std::ostringstream oss;
    oss << "  range [" << lo << ", " << hi
        << ") on thread " << std::this_thread::get_id() << "\n";
    std::cout << oss.str();
    // end::thread_id[]

    long long sum = 0;
    for (int i = lo; i < hi; ++i)
        sum += i;
    co_return capy::io_result<long long>{std::error_code(), sum};
}

int main()
{
    // tag::partition[]
    constexpr int total = 10000;
    constexpr int num_tasks = 4;
    constexpr int chunk = total / num_tasks;
    // end::partition[]

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

        // tag::when_all_variadic[]
        auto [ec, s0, s1, s2, s3] = co_await capy::when_all(
            partial_sum(0 * chunk, 1 * chunk),
            partial_sum(1 * chunk, 2 * chunk),
            partial_sum(2 * chunk, 3 * chunk),
            partial_sum(3 * chunk, 4 * chunk));
        // end::when_all_variadic[]

        long long total_sum = s0 + s1 + s2 + s3;

        // Arithmetic series: sum [0, N) = N*(N-1)/2
        long long expected =
            static_cast<long long>(total) * (total - 1) / 2;

        std::cout << "\nPartial sums: " << s0 << " + " << s1
                  << " + " << s2 << " + " << s3 << "\n";
        std::cout << "Total: " << total_sum
                  << " (expected " << expected << ")\n";
    };

    capy::run_async(pool.get_executor(), on_complete, on_error)(compute());
    done.wait();

    return 0;
}
// end::full[]
