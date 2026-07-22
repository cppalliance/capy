//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/3.concurrency/3c.advanced.adoc.

// tag::full[]
#include <iostream>
#include <thread>
#include <atomic>

std::atomic<int> counter{0};

void increment_many_times()
{
    for (int i = 0; i < 100000; ++i)
        ++counter;  // atomic increment
}

int main()
{
    std::thread t1(increment_many_times);
    std::thread t2(increment_many_times);

    t1.join();
    t2.join();

    std::cout << "Counter: " << counter << "\n";
    return 0;
}
// end::full[]
