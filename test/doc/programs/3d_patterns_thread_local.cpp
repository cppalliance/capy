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

thread_local int counter = 0;

void increment_and_print(char const* name)
{
    ++counter;
    std::cout << name << " counter: " << counter << "\n";
}

int main()
{
    std::thread t1([]{
        increment_and_print("T1");
        increment_and_print("T1");
    });

    std::thread t2([]{
        increment_and_print("T2");
        increment_and_print("T2");
    });

    t1.join();
    t2.join();

    return 0;
}
// end::full[]
