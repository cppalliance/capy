//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/quick-start.adoc.

// tag::full[]
#include <boost/capy/task.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <iostream>

namespace capy = boost::capy;

// A coroutine that returns a value
capy::task<int> answer()
{
    co_return 42;
}

// A coroutine that awaits another coroutine
capy::task<void> greet()
{
    int n = co_await answer();
    std::cout << "The answer is " << n << "\n";
}

int main()
{
    capy::thread_pool pool(1);

    // Start the coroutine on the pool's executor
    capy::run_async(pool.get_executor())(greet());

    // join() waits for outstanding work to complete; the pool
    // destructor only stops the pool and discards pending work
    pool.join();
}
// end::full[]
