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
#include <future>

int compute()
{
    return 6 * 7;
}

int main()
{
    std::future<int> future = std::async(compute);

    std::cout << "Computing...\n";
    int result = future.get();
    std::cout << "Result: " << result << "\n";

    return 0;
}
// end::full[]
