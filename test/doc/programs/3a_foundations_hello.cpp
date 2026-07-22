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
#include <iostream>
#include <thread>

void say_hello()
{
    std::cout << "Hello from a new thread!\n";
}

int main()
{
    std::thread t(say_hello);
    t.join();
    std::cout << "Back in the main thread.\n";
    return 0;
}
// end::full[]
