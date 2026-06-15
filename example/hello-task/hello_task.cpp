//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy.hpp>
#include <iostream>

namespace capy = boost::capy;

capy::task<> say_hello()
{
    std::cout << "Hello from Capy!\n";
    co_return;
}

int main()
{
    capy::thread_pool pool;
    capy::run_async(pool.get_executor())(say_hello());
    pool.join();
    return 0;
}
