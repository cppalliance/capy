//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/3.concurrency/3d.patterns.adoc.

// tag::full[]
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <functional>

void parallel_for(int start, int end, int num_threads,
                  std::function<void(int)> func)
{
    std::vector<std::thread> threads;
    int chunk_size = (end - start) / num_threads;

    for (int t = 0; t < num_threads; ++t)
    {
        int chunk_start = start + t * chunk_size;
        int chunk_end = (t == num_threads - 1) ? end : chunk_start + chunk_size;

        threads.emplace_back([=]{
            for (int i = chunk_start; i < chunk_end; ++i)
                func(i);
        });
    }

    for (auto& thread : threads)
        thread.join();
}

int main()
{
    std::mutex print_mutex;

    parallel_for(0, 20, 4, [&](int i){
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cout << "Processing " << i << " on thread "
                  << std::this_thread::get_id() << "\n";
    });

    return 0;
}
// end::full[]
