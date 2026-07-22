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
#include <string>

class Greeter
{
public:
    void greet(std::string const& name)
    {
        std::cout << "Hello, " << name << "!\n";
    }
};

int main()
{
    Greeter g;
    std::thread t(&Greeter::greet, &g, "World");
    t.join();
    return 0;
}
// end::full[]
