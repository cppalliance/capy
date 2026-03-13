//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "awaitable_sender.hpp"

#include <boost/capy.hpp>

#include <beman/execution/execution.hpp>

#include <chrono>
#include <iostream>
#include <latch>
#include <system_error>
#include <thread>

namespace capy = boost::capy;
namespace ex = beman::execution;

// A receiver whose environment carries a Capy executor.
// Completion signals a latch so main() can wait.
struct demo_receiver
{
    using receiver_concept = ex::receiver_t;

    capy::io_sender_env env_;
    std::latch* done_;

    auto get_env() const noexcept -> capy::io_sender_env
    {
        return env_;
    }

    void set_value() && noexcept
    {
        std::cout
            << "  set_value on thread "
            << std::this_thread::get_id() << "\n";
        done_->count_down();
    }

    void set_error(std::error_code ec) && noexcept
    {
        std::cerr << "  error: " << ec.message() << "\n";
        done_->count_down();
    }

    void set_error(std::exception_ptr ep) && noexcept
    {
        try { std::rethrow_exception(ep); }
        catch (std::exception const& e) {
            std::cerr << "  error: " << e.what() << "\n";
        }
        done_->count_down();
    }

    void set_stopped() && noexcept
    {
        std::cout << "  stopped\n";
        done_->count_down();
    }
};

int main()
{
    using namespace std::chrono_literals;

    std::cout
        << "main thread: "
        << std::this_thread::get_id() << "\n";

    // Capy execution context (provides timer service, etc.)
    capy::thread_pool pool;

    std::latch done(1);

    // Build a sender from a Capy IoAwaitable
    auto sndr = capy::as_sender(capy::delay(500ms));

    // Connect with a receiver whose environment carries
    // the Capy thread_pool executor
    auto op = ex::connect(
        std::move(sndr),
        demo_receiver{
            {pool.get_executor(), std::stop_token{}},
            &done});

    std::cout << "  starting delay...\n";
    ex::start(op);

    done.wait();
    std::cout << "  delay completed\n";
}
