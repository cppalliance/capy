//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/index.adoc, section Protocol Anatomy.

/*
    Runs the sequence drawn in the "How a Task Runs" diagram and records one
    trace entry per diagram arrow: the executor logs every dispatch, post and
    resume, the I/O awaitable logs its protocol steps, and the tasks log
    around each co_await. main() exits non-zero if the recorded order differs
    from the diagram, so a library change that alters the sequence (an extra
    executor hop, an inline resume) fails this test instead of silently
    falsifying the page.

    Everything runs on the main thread. The executor never resumes inline:
    dispatch() and post() both queue, and main() drains the queue. The fake
    I/O backend completes an operation only when the queue is empty, which is
    the "thread is free" state the diagram shows.
*/

// tag::full[]
#include <boost/capy.hpp>
#include <cstddef>
#include <functional>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

namespace capy = boost::capy;

std::vector<std::string> trace;

void step(std::string what)
{
    std::cout << trace.size() + 1 << ". " << what << "\n";
    trace.push_back(std::move(what));
}

// A fake I/O backend: pending operations complete when main() polls.
std::queue<std::function<void()>> backend;

class trace_loop : public capy::execution_context
{
    std::queue<capy::continuation> queue_;

public:
    class executor_type;

    trace_loop()
        : execution_context(this)
    {
    }

    // Nothing will run what is still queued: give it up, so its chain is
    // unwound. Destroying a service, or unwinding a chain, can queue more
    // work, so repeat until a drain finds nothing.
    ~trace_loop()
    {
        auto drain = [this]
        {
            bool any = false;
            while (!queue_.empty())
            {
                auto c = queue_.front();
                queue_.pop();
                any = true;
                capy::abandon(c);
            }
            return any;
        };
        shutdown();
        drain();
        do
        {
            destroy();
        }
        while (drain());
    }

    // A copy keeps the owner, which abandon needs.
    void enqueue(capy::continuation const& c)
    {
        queue_.push(c);
    }

    // Run queued coroutines. Poll the backend only when the queue is empty.
    void run()
    {
        while (!queue_.empty() || !backend.empty())
        {
            if (queue_.empty())
            {
                auto complete = std::move(backend.front());
                backend.pop();
                complete();
                continue;
            }
            auto c = queue_.front();
            queue_.pop();
            step("executor: resume");
            capy::safe_resume(c.h);
        }
    }

    executor_type get_executor() noexcept;
};

class trace_loop::executor_type
{
    trace_loop* loop_ = nullptr;

public:
    executor_type() = default;

    explicit executor_type(trace_loop& loop) noexcept
        : loop_(&loop)
    {
    }

    capy::execution_context& context() const noexcept
    {
        return *loop_;
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<> dispatch(capy::continuation& c) const
    {
        step("executor: dispatch");
        loop_->enqueue(c);
        return std::noop_coroutine();
    }

    void post(capy::continuation& c) const
    {
        step("executor: post");
        loop_->enqueue(c);
    }

    bool operator==(executor_type const& other) const noexcept
    {
        return loop_ == other.loop_;
    }
};

trace_loop::executor_type
trace_loop::get_executor() noexcept
{
    return executor_type{*this};
}

// An I/O awaitable for a read that the fake backend completes with 5 bytes.
struct read_awaitable
{
    capy::continuation cont_;
    capy::executor_ref ex_;
    std::size_t bytes_ = 0;

    bool await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h, capy::await_context const* env)
    {
        cont_.h = h;
        cont_.owner = env->owner;
        ex_ = env->executor;
        step("I/O awaitable: start the operation");
        backend.push([this] {
            step("I/O backend: the operation completes");
            bytes_ = 5;
            ex_.post(cont_);  // never cont_.h.resume()
        });
        step("I/O awaitable: suspend");
        return std::noop_coroutine();
    }

    capy::results<std::size_t> await_resume() const noexcept
    {
        return {std::error_code(), bytes_};
    }
};

capy::task<std::size_t> child()
{
    step("child: co_await read_some");
    auto [ec, n] = co_await read_awaitable{};
    step("child: co_return");
    co_return ec ? 0 : n;
}

capy::task<std::size_t> my_task()
{
    step("my_task: co_await child()");
    std::size_t n = co_await child();
    step("my_task: co_return");
    co_return n;
}

int main()
{
    trace_loop loop;

    step("caller: run_async(ex)(my_task())");
    capy::run_async(loop.get_executor(), [](std::size_t n) {
        step("run_async: call the completion handler with " +
            std::to_string(n));
    })(my_task());

    loop.run();

    // One entry per arrow in the diagram, in the same order.
    std::vector<std::string> const diagram = {
        "caller: run_async(ex)(my_task())",              //  1
        "executor: dispatch",                            //  2
        "executor: resume",                              //  3
        "my_task: co_await child()",                     //  4
        "child: co_await read_some",                     //  5
        "I/O awaitable: start the operation",            //  6
        "I/O awaitable: suspend",                        //  7
        "I/O backend: the operation completes",          //  8
        "executor: post",                                //  9
        "executor: resume",                              // 10
        "child: co_return",                              // 11
        "my_task: co_return",                            // 12
        "run_async: call the completion handler with 5", // 13
    };
    if (trace != diagram)
    {
        std::cout << "trace does not match the diagram\n";
        return 1;
    }
}
// end::full[]
