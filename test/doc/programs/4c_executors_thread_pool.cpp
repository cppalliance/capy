//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/4.coroutines/4c.executors.adoc.

// tag::full[]
#include <boost/capy/ex/thread_pool.hpp>

// end::full[]
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>

using namespace boost::capy;

task<void> my_task()
{
    co_return;
}

// tag::full[]
int main()
{
    // Create pool with 4 threads
    thread_pool pool(4);

    // Get an executor for this pool
    auto ex = pool.get_executor();

    // Launch work on the pool
    run_async(ex)(my_task());

    pool.join();  // wait for outstanding work to complete
}
// end::full[]
