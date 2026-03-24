//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Custom Executor Example
//
// Implements the Executor concept with a simple single-threaded
// run loop, similar to a GUI event loop.  Shows that Capy is not
// tied to thread_pool and can integrate with any scheduling system.
//

#include <boost/capy.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <iostream>
#include <queue>
#include <thread>
#include <vector>

namespace capy = boost::capy;

// A minimal single-threaded execution context.
// Demonstrates how to satisfy the Executor concept
// for any custom scheduling system.
class run_loop : public capy::execution_context
{
    std::queue<std::coroutine_handle<>> queue_;
    std::thread::id owner_;

public:
    class executor_type;

    run_loop()
        : execution_context(this)
    {
    }

    ~run_loop()
    {
        shutdown();
        destroy();
    }

    run_loop(run_loop const&) = delete;
    run_loop& operator=(run_loop const&) = delete;

    // Drain the queue until empty
    void run()
    {
        owner_ = std::this_thread::get_id();
        while (!queue_.empty())
        {
            auto h = queue_.front();
            queue_.pop();
            capy::safe_resume(h);
        }
    }

    void enqueue(std::coroutine_handle<> h)
    {
        queue_.push(h);
    }

    bool is_running_on_this_thread() const noexcept
    {
        return std::this_thread::get_id() == owner_;
    }

    executor_type get_executor() noexcept;
};

class run_loop::executor_type
{
    friend class run_loop;
    run_loop* loop_ = nullptr;

    explicit executor_type(run_loop& loop) noexcept
        : loop_(&loop)
    {
    }

public:
    executor_type() = default;

    capy::execution_context& context() const noexcept
    {
        return *loop_;
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<> dispatch(
        capy::continuation& c) const
    {
        if (loop_->is_running_on_this_thread())
            return c.h;
        loop_->enqueue(c.h);
        return std::noop_coroutine();
    }

    void post(capy::continuation& c) const
    {
        loop_->enqueue(c.h);
    }

    bool operator==(executor_type const& other) const noexcept
    {
        return loop_ == other.loop_;
    }
};

inline
run_loop::executor_type
run_loop::get_executor() noexcept
{
    return executor_type{*this};
}

// Verify the concept is satisfied
static_assert(capy::Executor<run_loop::executor_type>);

capy::io_task<int> compute(int x)
{
    std::cout << "  computing " << x << " * " << x << "\n";
    co_return capy::io_result<int>{{}, x * x};
}

capy::task<> run_tasks()
{
    std::cout << "Launching 3 tasks with when_all...\n";

    auto [ec, r1, r2, r3] = co_await capy::when_all(
        compute(3), compute(7), compute(11));

    std::cout << "\nResults: " << r1 << ", " << r2
              << ", " << r3 << "\n";
    std::cout << "Sum of squares: "
              << r1 + r2 + r3 << "\n";
}

int main()
{
    run_loop loop;

    // Launch using run_async, just like with thread_pool
    capy::run_async(loop.get_executor())(run_tasks());

    // Drive the loop — all coroutines execute here
    std::cout << "Running event loop on main thread...\n";
    loop.run();

    std::cout << "Event loop finished.\n";
    return 0;
}
