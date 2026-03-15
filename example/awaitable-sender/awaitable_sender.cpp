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
#include <stop_token>
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

    // Test cancellation via stop token
    std::cout << "\n--- cancellation test ---\n";
    std::stop_source ss;
    std::latch done2(1);

    auto sndr2 = capy::as_sender(capy::delay(5s));
    auto op2 = ex::connect(
        std::move(sndr2),
        demo_receiver{
            {pool.get_executor(), ss.get_token()},
            &done2});

    std::cout << "  starting 5s delay...\n";
    ex::start(op2);

    std::this_thread::sleep_for(100ms);
    std::cout << "  requesting stop...\n";
    ss.request_stop();

    done2.wait();
    std::cout << "  cancellation test done\n";

    // Test split_ec with success (error_code == 0)
    std::cout << "\n--- split_ec success test ---\n";
    std::latch done3(1);

    auto sndr3 = capy::split_ec(
        capy::as_sender(capy::delay(100ms)));
    auto op3 = ex::connect(
        std::move(sndr3),
        demo_receiver{
            {pool.get_executor(), std::stop_token{}},
            &done3});

    ex::start(op3);
    done3.wait();
    std::cout << "  split_ec success test done\n";

    // Test split_ec with error (error_code != 0)
    std::cout << "\n--- split_ec error test ---\n";
    std::latch done4(1);

    auto make_ec_sender = [&pool]() {
        auto task = [](capy::executor_ref)
            -> capy::task<std::error_code>
        {
            co_return std::make_error_code(
                std::errc::connection_reset);
        }(pool.get_executor());
        return capy::as_sender(std::move(task));
    };

    auto sndr4 = capy::split_ec(make_ec_sender());
    auto op4 = ex::connect(
        std::move(sndr4),
        demo_receiver{
            {pool.get_executor(), std::stop_token{}},
            &done4});

    ex::start(op4);
    done4.wait();
    std::cout << "  split_ec error test done\n";
}
