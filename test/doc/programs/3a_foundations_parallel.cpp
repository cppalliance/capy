//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/3.concurrency/3a.foundations.adoc.
// Output interleaving varies run to run; that is the point of the demo.

// tag::full[]
#include <iostream>
#include <thread>

void count_up(char const* name)
{
    for (int i = 1; i <= 5; ++i)
        std::cout << name << ": " << i << "\n";
}

int main()
{
    std::thread alice(count_up, "Alice");
    std::thread bob(count_up, "Bob");

    alice.join();
    bob.join();

    return 0;
}
// end::full[]
