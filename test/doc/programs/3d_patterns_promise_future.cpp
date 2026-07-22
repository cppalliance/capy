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
#include <future>

void compute(std::promise<int> result_promise)
{
    int answer = 6 * 7;  // expensive computation
    result_promise.set_value(answer);
}

int main()
{
    std::promise<int> promise;
    std::future<int> future = promise.get_future();

    std::thread t(compute, std::move(promise));

    std::cout << "Waiting for result...\n";
    int result = future.get();  // blocks until value is set
    std::cout << "The answer is: " << result << "\n";

    t.join();
    return 0;
}
// end::full[]
