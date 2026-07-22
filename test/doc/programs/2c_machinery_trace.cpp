//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/2.cpp20-coroutines/2c.machinery.adoc.

// tag::full[]
#include <coroutine>
#include <iostream>

struct TracePromise
{
    struct promise_type
    {
        promise_type()
        {
            std::cout << "promise constructed" << std::endl;
        }

        ~promise_type()
        {
            std::cout << "promise destroyed" << std::endl;
        }

        TracePromise get_return_object()
        {
            std::cout << "get_return_object called" << std::endl;
            return TracePromise{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend()
        {
            std::cout << "initial_suspend called" << std::endl;
            return {};
        }

        std::suspend_always final_suspend() noexcept
        {
            std::cout << "final_suspend called" << std::endl;
            return {};
        }

        void return_void()
        {
            std::cout << "return_void called" << std::endl;
        }

        void unhandled_exception()
        {
            std::cout << "unhandled_exception called" << std::endl;
        }
    };

    std::coroutine_handle<promise_type> handle;

    explicit TracePromise(std::coroutine_handle<promise_type> h)
        : handle(h)
    {
    }

    // final_suspend() returns suspend_always, so the frame stays alive
    // until the handle is destroyed explicitly.
    ~TracePromise()
    {
        std::cout << "destroying coroutine handle" << std::endl;
        handle.destroy();
    }
};

TracePromise trace_coroutine()
{
    std::cout << "coroutine body begins" << std::endl;
    co_return;
}

int main()
{
    std::cout << "calling coroutine" << std::endl;
    [[maybe_unused]] auto result = trace_coroutine();
    std::cout << "coroutine returned" << std::endl;
}
// end::full[]
