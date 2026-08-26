//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/4.coroutines/4b.launching.adoc.

// tag::full[]
#include <boost/capy.hpp>
namespace capy = boost::capy;

capy::task<int> compute()
{
    co_return 42;
}

int main()
{
    capy::thread_pool pool;
    capy::run_async(pool.get_executor())(compute());
    // Task is now running on the thread pool

    pool.join();  // wait for outstanding work to complete
}
// end::full[]
