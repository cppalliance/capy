//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/2.cpp20-coroutines/2b.syntax.adoc.

// tag::full[]
#include <coroutine>
#include <iostream>

struct ReturnObject
{
    struct promise_type
    {
        ReturnObject get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

struct Awaiter
{
    std::coroutine_handle<>* handle_out;

    bool await_ready() { return false; }  // always suspend

    void await_suspend(std::coroutine_handle<> h)
    {
        *handle_out = h;  // store handle for later resumption
    }

    void await_resume() {}  // nothing to return
};

ReturnObject counter(std::coroutine_handle<>* handle)
{
    Awaiter awaiter{handle};

    for (unsigned i = 0; ; ++i)
    {
        std::cout << "counter: " << i << std::endl;
        co_await awaiter;
    }
}

int main()
{
    std::coroutine_handle<> h;
    counter(&h);

    for (int i = 0; i < 3; ++i)
    {
        std::cout << "main: resuming" << std::endl;
        h();
    }

    h.destroy();
}
// end::full[]
