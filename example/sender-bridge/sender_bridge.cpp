//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "sender_awaitable.hpp"

#include <boost/capy.hpp>

#include <beman/execution/execution.hpp>

#include <iostream>
#include <latch>
#include <thread>

namespace capy = boost::capy;
namespace ex = beman::execution;

capy::task<int> compute(auto sched)
{
    int result = co_await capy::await_sender(
        ex::schedule(sched)
            | ex::then([] {
                std::cout
                    << "  sender running on thread "
                    << std::this_thread::get_id() << "\n";
                return 42 * 42;
            }));

    std::cout
        << "  coroutine resumed on thread "
        << std::this_thread::get_id() << "\n";

    co_return result;
}

int main()
{
    std::cout
        << "main thread: "
        << std::this_thread::get_id() << "\n";

    // Capy execution context
    capy::thread_pool pool;

    // Beman execution context (run_loop on a dedicated thread)
    ex::run_loop loop;
    std::jthread loop_thread([&loop] {
        loop.run();
    });
    auto sched = loop.get_scheduler();

    std::latch done(1);
    int answer = 0;

    auto on_complete = [&](int v) {
        answer = v;
        done.count_down();
    };

    auto on_error = [&](std::exception_ptr ep) {
        try { std::rethrow_exception(ep); }
        catch (std::exception const& e) {
            std::cerr << "error: " << e.what() << "\n";
        }
        done.count_down();
    };

    capy::run_async(
        pool.get_executor(),
        on_complete,
        on_error)(compute(sched));

    done.wait();
    loop.finish();

    std::cout << "result: " << answer << "\n";
}
