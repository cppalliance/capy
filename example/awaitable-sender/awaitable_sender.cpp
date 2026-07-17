//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "awaitable_sender.hpp"
#include "awaitable_sender_base.hpp"
#include "read_op.hpp"

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

    // Capy execution context. Single-threaded: async_waker's
    // wait() requires resumption on a single thread.
    capy::thread_pool pool(1);

    // Named so io_sender_env's executor_ref (which stores a
    // pointer to whatever it is given) points at a stable object
    // instead of a temporary that dies at the end of the
    // full-expression that constructs each demo_receiver.
    auto pool_ex = pool.get_executor();

    // Escape-hatch timing: a helper thread plays the clock and
    // wakes waker_1 and waker_3 after a short delay.
    // waker_2 is deliberately never woken, so its demo below
    // completes only via stop-token cancellation, not a wakeup.
    capy::async_waker waker_1, waker_2, waker_3;
    std::thread waker_thread([&] {
        std::this_thread::sleep_for(50ms);
        waker_1.wake();
        std::this_thread::sleep_for(50ms);
        waker_3.wake();
    });

    std::latch done(1);

    // Build a sender from a Capy IoAwaitable
    auto sndr = capy::as_sender(waker_1.wait());

    // Connect with a receiver whose environment carries
    // the Capy thread_pool executor
    auto op = ex::connect(
        std::move(sndr),
        demo_receiver{
            {pool_ex, std::stop_token{}},
            &done});

    std::cout << "  starting wait...\n";
    ex::start(op);

    done.wait();
    std::cout << "  wait completed\n";

    // Test cancellation via stop token
    std::cout << "\n--- cancellation test ---\n";
    std::stop_source ss;
    std::latch done2(1);

    auto sndr2 = capy::as_sender(waker_2.wait());
    auto op2 = ex::connect(
        std::move(sndr2),
        demo_receiver{
            {pool_ex, ss.get_token()},
            &done2});

    std::cout << "  starting wait (never woken)...\n";
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
        capy::as_sender(waker_3.wait()));
    auto op3 = ex::connect(
        std::move(sndr3),
        demo_receiver{
            {pool_ex, std::stop_token{}},
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
            {pool_ex, std::stop_token{}},
            &done4});

    ex::start(op4);
    done4.wait();
    std::cout << "  split_ec error test done\n";

    // A native awaitable-sender: read_op derives
    // awaitable_sender_base, no as_sender() wrapping.
    std::cout << "\n--- native awaitable-sender test ---\n";
    std::latch done5(1);

    auto rop = capy::read_op::result({}, 42);
    auto op5 = ex::connect(
        ex::then(
            std::move(rop),
            [](std::size_t n)
            {
                std::cout
                    << "  read " << n << " bytes\n";
            }),
        demo_receiver{
            {pool_ex, std::stop_token{}},
            &done5});

    ex::start(op5);
    done5.wait();
    std::cout << "  native awaitable-sender test done\n";

    // All demos have drained; safe to join the waker thread now.
    waker_thread.join();
}
