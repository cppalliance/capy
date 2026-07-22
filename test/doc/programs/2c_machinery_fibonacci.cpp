//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Fibonacci generator shown in pages/2.cpp20-coroutines/2c.machinery.adoc.
// The Generator type below is the one built earlier on the same page;
// only the tagged region is displayed.

#include <coroutine>
#include <exception>
#include <iostream>

struct Generator
{
    struct promise_type
    {
        int current_value;

        Generator get_return_object()
        {
            return Generator{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(int value)
        {
            current_value = value;
            return {};
        }

        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;

    Generator(std::coroutine_handle<promise_type> h) : handle(h) {}

    ~Generator()
    {
        if (handle)
            handle.destroy();
    }

    Generator(Generator const&) = delete;
    Generator& operator=(Generator const&) = delete;

    Generator(Generator&& other) noexcept
        : handle(other.handle)
    {
        other.handle = nullptr;
    }

    Generator& operator=(Generator&& other) noexcept
    {
        if (this != &other)
        {
            if (handle)
                handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    bool next()
    {
        if (!handle || handle.done())
            return false;
        handle.resume();
        return !handle.done();
    }

    int value() const
    {
        return handle.promise().current_value;
    }
};

// tag::full[]
Generator fibonacci()
{
    int a = 0, b = 1;
    while (true)
    {
        co_yield a;
        int next = a + b;
        a = b;
        b = next;
    }
}

int main()
{
    auto fib = fibonacci();

    for (int i = 0; i < 10 && fib.next(); ++i)
    {
        std::cout << fib.value() << " ";
    }
    std::cout << std::endl;
}
// end::full[]
