//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/ex/detail/timer_service.hpp>

namespace boost {
namespace capy {
namespace detail {

timer_service::
timer_service(execution_context& ctx)
    : thread_([this] { run(); })
{
    (void)ctx;
}

timer_service::
~timer_service()
{
    stop_and_join();
}

timer_service::timer_id
timer_service::
schedule_at(
    std::chrono::steady_clock::time_point deadline,
    std::function<void()> cb)
{
    std::lock_guard lock(mutex_);
    auto id = ++next_id_;
    active_ids_.insert(id);
    queue_.push(entry{deadline, id, std::move(cb)});
    cv_.notify_one();
    return id;
}

void
timer_service::
cancel(timer_id id)
{
    std::unique_lock lock(mutex_);
    if(!active_ids_.contains(id))
        return;
    if(executing_id_ == id)
    {
        // Callback is running — wait for it to finish.
        // run() erases from active_ids_ after execution.
        while(executing_id_ == id)
            cancel_cv_.wait(lock);
        return;
    }
    active_ids_.erase(id);
}

void
timer_service::
stop_and_join()
{
    {
        std::lock_guard lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_one();
    if(thread_.joinable())
        thread_.join();
}

void
timer_service::
shutdown()
{
    stop_and_join();
}

void
timer_service::
run()
{
    std::unique_lock lock(mutex_);
    for(;;)
    {
        if(stopped_)
            return;

        if(queue_.empty())
        {
            cv_.wait(lock);
            continue;
        }

        auto deadline = queue_.top().deadline;
        auto now = std::chrono::steady_clock::now();
        if(deadline > now)
        {
            cv_.wait_until(lock, deadline);
            continue;
        }

        // Pop the entry (const_cast needed because priority_queue::top is const)
        auto e = std::move(const_cast<entry&>(queue_.top()));
        queue_.pop();

        // Skip if cancelled (no longer in active set)
        if(!active_ids_.contains(e.id))
            continue;

        executing_id_ = e.id;
        lock.unlock();
        e.callback();
        lock.lock();
        active_ids_.erase(e.id);
        executing_id_ = 0;
        cancel_cv_.notify_all();
    }
}

} // detail
} // capy
} // boost
