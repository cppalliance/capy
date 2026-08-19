//
// Copyright (c) 2026 Mungo Gill
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// tag::full[]
#include <boost/capy.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <latch>
#include <thread>

namespace capy = boost::capy;

// A slow operation that respects cancellation
capy::task<std::string> slow_fetch(int steps)
{
    // tag::get_stop_token[]
    auto token = co_await capy::this_coro::stop_token;  // std::stop_token
    // end::get_stop_token[]
    std::string result;

    for (int i = 0; i < steps; ++i)
    {
        // Check cancellation before each step
        // tag::check_stop[]
        if (token.stop_requested())
        {
            std::cout << "  Cancelled at step " << i << std::endl;
            throw std::system_error(
                make_error_code(std::errc::operation_canceled));
        }
        // end::check_stop[]

        result += "step" + std::to_string(i) + " ";

        // Simulate slow work (in real code, this would be I/O)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::cout << "  Completed step " << i << std::endl;

        // Yield to allow stop request to be processed before next check
        // Extra 5ms ensures print completes before main thread prints
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    co_return result;
}

// Shared between the fetch worker thread and the coroutine side of
// the race in demo_timeout below.
// tag::fetch_channel[]
struct fetch_channel
{
    capy::async_waker fetch_ready;
    std::atomic<bool> cancelled{false};
    std::string result;
};
// end::fetch_channel[]

// One side of the race: completes when the fetch worker thread
// wakes fetch_ready. If the deadline wins first, when_any's stop
// request cancels the wait; flag the worker so it stops early.
// tag::race_await_fetch[]
capy::io_task<std::string> await_fetch(fetch_channel& ch)
{
    auto [ec] = co_await ch.fetch_ready.wait();
    if (ec)
    {
        ch.cancelled.store(true);
        co_return capy::io_result<std::string>{ec, {}};
    }
    co_return capy::io_result<std::string>{std::error_code(), std::move(ch.result)};
}
// end::race_await_fetch[]

// The other side of the race: completes once a user thread wakes
// the waker. This is the escape hatch: the user supplies the
// thread and the clock.
// tag::race_deadline[]
capy::io_task<> deadline(capy::async_waker& waker)
{
    auto [ec] = co_await waker.wait();
    co_return capy::io_result<>{ec};
}
// end::race_deadline[]

// Wraps slow_fetch, translating a cancellation exception into
// std::nullopt for the manual stop_token demo below.
capy::task<std::optional<std::string>> fetch_or_cancelled()
{
    try
    {
        auto result = co_await slow_fetch(5);  // std::string
        co_return result;
    }
    catch (std::system_error const& e)
    {
        if (e.code() == std::errc::operation_canceled)
            co_return std::nullopt;
        throw;
    }
}

void demo_timeout()
{
    std::cout << "Demo: Fetch races a deadline\n";

    // Both when_any children are plain waker waits, so the whole
    // race runs on one thread, as async_waker requires. The
    // blocking work happens on user threads that wake them.
    capy::thread_pool pool(1);
    std::latch done(1);

    fetch_channel fetch_ch;
    capy::async_waker deadline_waker;

    // Worker thread does the slow fetch and wakes fetch_ready on
    // completion, checking the cancel flag between steps so a lost race stops
    // the work promptly. Mirrors slow_fetch's step loop, kept
    // separate since one polls an atomic and the other a stop token.
    std::thread fetch_thread([&fetch_ch] {
        for (int i = 0; i < 5; ++i)
        {
            if (fetch_ch.cancelled.load())
            {
                std::cout << "  Cancelled at step " << i << std::endl;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            fetch_ch.result += "step" + std::to_string(i) + " ";
            std::cout << "  Completed step " << i << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        fetch_ch.fetch_ready.wake();
    });

    // Deadline thread: a user thread plays the clock, waking the
    // waker after the allotted time.
    std::thread deadline_thread([&deadline_waker] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        deadline_waker.wake();
    });

    // Index 1 = fetch won, index 2 = deadline won, index 0 = error.
    std::size_t winner = 0;
    std::string fetch_result;

    // tag::race_lambda[]
    auto race = [&]() -> capy::task<>
    {
        // tag::race[]
        auto result = co_await capy::when_any(
            await_fetch(fetch_ch),
            deadline(deadline_waker));
        // end::race[]
        winner = result.index();
        if (result.index() == 1)
            fetch_result = std::get<1>(std::move(result));
    };
    // end::race_lambda[]

    capy::run_async(pool.get_executor(),
        [&done]() { done.count_down(); },
        [&done](std::exception_ptr) { done.count_down(); }
    )(race());

    done.wait();  // Block until the race completes
    fetch_thread.join();
    deadline_thread.join();

    // Report after the joins so the worker's final line lands
    // before the verdict.
    if (winner == 1)
        // This demo's timing always favors the deadline; this branch
        // shows how a fetch win would be consumed if it ever happened.
        std::cout << "Result: " << fetch_result << "\n";
    else if (winner == 2)
        std::cout << "Timed out waiting for fetch\n";
    else
        std::cout << "Error\n";
}

void demo_cancellation()
{
    std::cout << "\nDemo: Cancellation after 2 steps\n";

    capy::thread_pool pool;
    std::stop_source source;
    std::latch done(1);  // std::latch - wait for 1 task

    // Start the task
    capy::run_async(pool.get_executor(), source.get_token(),
        [&done](std::optional<std::string> result) {
            if (result)
                std::cout << "Result: " << *result << "\n";
            else
                std::cout << "Cancelled (returned nullopt)\n";
            done.count_down();
        },
        [&done](std::exception_ptr) { done.count_down(); }
    )(fetch_or_cancelled());

    // Simulate timeout: cancel after 2 steps complete
    // Timing: each step is 10ms work + 15ms yield = 25ms total
    // Step 1 prints at 35ms, step 2 check at 50ms
    // Stop at 42ms: after step 1 print, before step 2 check
    std::this_thread::sleep_for(std::chrono::milliseconds(42));
    std::cout << "  Requesting stop..." << std::endl;
    source.request_stop();

    done.wait();  // Block until task completes (after cancellation)
}

// Example: Manual stop token checking
capy::task<int> process_items(std::vector<int> const& items)
{
    auto token = co_await capy::this_coro::stop_token;  // std::stop_token
    int sum = 0;

    for (auto item : items)  // int
    {
        // tag::partial_result[]
        if (token.stop_requested())
        {
            std::cout << "Processing cancelled, partial sum: " << sum << "\n";
            co_return sum;  // Return partial result
        }
        // end::partial_result[]

        sum += item;
    }

    co_return sum;
}

int main()
{
    demo_timeout();
    demo_cancellation();

    return 0;
}
// end::full[]
