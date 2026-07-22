//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/2.cpp20-coroutines/2d.advanced.adoc.

// tag::full[]
#include <coroutine>
#include <exception>
#include <iostream>
#include <stdexcept>

struct Task
{
    struct promise_type
    {
        std::exception_ptr exception;

        Task get_return_object()
        {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}

        void unhandled_exception()
        {
            exception = std::current_exception();
        }
    };

    std::coroutine_handle<promise_type> handle;

    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Task() { if (handle) handle.destroy(); }

    void run() { handle.resume(); }

    void check_exception()
    {
        if (handle.promise().exception)
            std::rethrow_exception(handle.promise().exception);
    }
};

Task risky_operation()
{
    std::cout << "Starting risky operation" << std::endl;
    throw std::runtime_error("Something went wrong");
    co_return;  // never reached
}

int main()
{
    Task task = risky_operation();

    try
    {
        task.run();
        task.check_exception();
        std::cout << "Operation completed successfully" << std::endl;
    }
    catch (std::exception const& e)
    {
        std::cout << "Operation failed: " << e.what() << std::endl;
    }
}
// end::full[]
