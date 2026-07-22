//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/4.coroutines/4c.executors.adoc.

#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/task.hpp>

#include <coroutine>
#include <semaphore>

using namespace boost::capy;

namespace {

continuation parked;
std::binary_semaphore parked_ready{0};

// Suspends its coroutine and publishes the continuation so another
// thread can schedule the resumption.
struct park
{
    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h, io_env const*)
    {
        parked.h = h;
        parked_ready.release();
        return std::noop_coroutine();
    }

    void await_resume() {}
};

task<void> parked_task()
{
    co_await park{};
}

// Launch a coroutine on the pool and block until it has parked,
// leaving its continuation ready to be scheduled.
continuation& make_suspended_work(thread_pool& pool)
{
    run_async(pool.get_executor())(parked_task());
    parked_ready.acquire();
    return parked;
}

} // namespace

// tag::full[]
void schedule_work(executor_ref ex, continuation& c)
{
    ex.post(c);  // Works with any executor
}

int main()
{
    thread_pool pool;
    auto pool_ex = pool.get_executor();
    executor_ref ex = pool_ex;  // Type erasure; pool_ex must outlive ex

    continuation& c = make_suspended_work(pool);  // a coroutine parked on the pool
    schedule_work(ex, c);
    pool.join();
}
// end::full[]
