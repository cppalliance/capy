//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/3.concurrency/3a.foundations.adoc.

// tag::full[]
#include <functional>
#include <iostream>
#include <thread>

void increment(int& value)
{
    ++value;
}

int main()
{
    int counter = 0;

    std::thread t(increment, std::ref(counter));
    t.join();

    std::cout << "Counter is now: " << counter << "\n";
    return 0;
}
// end::full[]
