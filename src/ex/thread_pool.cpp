//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/capy
//

#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/detail/thread_local_ptr.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/test/thread_name.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

/*
    Thread pool implementation using a shared work queue.

    Work items are continuations linked via their intrusive next pointer,
    stored in a single queue protected by a mutex. No per-post heap
    allocation: the continuation is owned by the caller and linked
    directly. Worker threads wait on a condition_variable until work
    is available or stop is requested.

    Threads are started lazily on first post() via std::call_once to avoid
    spawning threads for pools that are constructed but never used. Each
    thread is named with a configurable prefix plus index for debugger
    visibility.

    Work tracking: on_work_started/on_work_finished maintain the atomic
    outstanding_work_ counter. on_work_started is lock-free; the worker
    that drives the count to zero takes mutex_ and re-reads the count
    before deciding to stop, so the count and the stop decision stay
    consistent even if work is started in between. join() blocks until
    this counter reaches zero, then signals workers to stop and joins
    threads.

    Two shutdown paths:
    - join(): waits for outstanding work to drain, then stops workers.
    - stop(): immediately signals workers to exit; queued work is abandoned.
    - Destructor: stop() then join() (abandon + wait for threads).
*/

namespace boost {
namespace capy {

//------------------------------------------------------------------------------

class thread_pool::impl
{
    // Identifies the pool owning the current worker thread, or
    // nullptr if the calling thread is not a pool worker. Checked
    // by dispatch() to decide between symmetric transfer (inline
    // resume) and post.
    static inline detail::thread_local_ptr<impl const> current_;

    // Intrusive queue of continuations: the next link is stored in
    // continuation::reserved (typed continuation* round-tripped through
    // void*). No per-post allocation: the continuation is owned by the caller.
    continuation* head_ = nullptr;
    continuation* tail_ = nullptr;

    void push(continuation* c) noexcept
    {
        c->reserved = nullptr;
        if(tail_)
            tail_->reserved = c;
        else
            head_ = c;
        tail_ = c;
    }

    continuation* pop() noexcept
    {
        if(!head_)
            return nullptr;
        continuation* c = head_;
        head_ = static_cast<continuation*>(head_->reserved);
        if(!head_)
            tail_ = nullptr;
        return c;
    }

    bool empty() const noexcept
    {
        return head_ == nullptr;
    }

    std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable done_cv_;
    std::vector<std::thread> threads_;
    std::atomic<std::size_t> outstanding_work_{0};
    bool stop_{false};
    bool joined_{false};
    std::size_t num_threads_;
    char thread_name_prefix_[13]{};  // 12 chars max + null terminator
    std::once_flag start_flag_;

public:
    ~impl() = default;

    bool
    running_in_this_thread() const noexcept
    {
        return current_.get() == this;
    }

    // Destroy abandoned coroutine frames. Must be called
    // before execution_context::shutdown()/destroy() so
    // that suspended-frame destructors touching services
    // (e.g. cancelling registrations) run while those
    // services are still valid.
    void
    drain_abandoned() noexcept
    {
        while(auto* c = pop())
        {
            auto h = c->h;
            if(h && h != std::noop_coroutine())
                h.destroy();
        }
    }

    impl(std::size_t num_threads, std::string_view thread_name_prefix)
        : num_threads_(num_threads)
    {
        if(num_threads_ == 0)
            num_threads_ = std::max(
                std::thread::hardware_concurrency(), 1u);

        // Truncate prefix to 12 chars, leaving room for up to 3-digit index.
        auto n = thread_name_prefix.copy(thread_name_prefix_, 12);
        thread_name_prefix_[n] = '\0';
    }

    void
    post(continuation& c)
    {
        ensure_started();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            push(&c);
            // Under the lock so the pool cannot drain, join, and
            // destroy the condition variable mid-signal.
            work_cv_.notify_one();
        }
    }

    void
    on_work_started() noexcept
    {
        outstanding_work_.fetch_add(1, std::memory_order_acq_rel);
    }

    void
    on_work_finished() noexcept
    {
        if(outstanding_work_.fetch_sub(
            1, std::memory_order_acq_rel) == 1)
        {
            // fetch_sub's result can be stale: a concurrent
            // on_work_started() may raise the count before we take the
            // lock, so re-read it here rather than trust the decrement.
            std::lock_guard<std::mutex> lock(mutex_);
            if(outstanding_work_.load(
                std::memory_order_acquire) == 0 && joined_ && !stop_)
            {
                stop_ = true;
                done_cv_.notify_all();
                work_cv_.notify_all();
            }
        }
    }

    void
    join() noexcept
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if(joined_)
                return;
            joined_ = true;

            if(outstanding_work_.load(
                std::memory_order_acquire) == 0)
            {
                stop_ = true;
                work_cv_.notify_all();
            }
            else
            {
                done_cv_.wait(lock, [this]{
                    return stop_;
                });
            }
        }

        for(auto& t : threads_)
            if(t.joinable())
                t.join();
    }

    void
    stop() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        work_cv_.notify_all();
        done_cv_.notify_all();
    }

private:
    void
    ensure_started()
    {
        std::call_once(start_flag_, [this]{
            threads_.reserve(num_threads_);
            for(std::size_t i = 0; i < num_threads_; ++i)
                threads_.emplace_back([this, i]{ run(i); });
        });
    }

    void
    run(std::size_t index)
    {
        // Build name; set_current_thread_name truncates to platform limits.
        char name[16];
        std::snprintf(name, sizeof(name), "%s%zu", thread_name_prefix_, index);
        set_current_thread_name(name);

        // Mark this thread as a worker of this pool so dispatch()
        // can symmetric-transfer when called from within pool work.
        struct scoped_pool
        {
            scoped_pool(impl const* p) noexcept { current_.set(p); }
            ~scoped_pool() noexcept { current_.set(nullptr); }
        } guard(this);

        for(;;)
        {
            continuation* c = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                work_cv_.wait(lock, [this]{
                    return !empty() ||
                        stop_;
                });
                if(stop_)
                    return;
                c = pop();
            }
            if(c)
                safe_resume(c->h);
        }
    }
};

//------------------------------------------------------------------------------

thread_pool::
~thread_pool()
{
    impl_->stop();
    impl_->join();
    impl_->drain_abandoned();
    shutdown();
    destroy();
    delete impl_;
}

thread_pool::
thread_pool(std::size_t num_threads, std::string_view thread_name_prefix)
    : impl_(new impl(num_threads, thread_name_prefix))
{
    this->set_frame_allocator(std::allocator<void>{});
}

void
thread_pool::
join() noexcept
{
    impl_->join();
}

void
thread_pool::
stop() noexcept
{
    impl_->stop();
}

//------------------------------------------------------------------------------

thread_pool::executor_type
thread_pool::
get_executor() const noexcept
{
    return executor_type(
        const_cast<thread_pool&>(*this));
}

void
thread_pool::executor_type::
on_work_started() const noexcept
{
    pool_->impl_->on_work_started();
}

void
thread_pool::executor_type::
on_work_finished() const noexcept
{
    pool_->impl_->on_work_finished();
}

void
thread_pool::executor_type::
post(continuation& c) const
{
    pool_->impl_->post(c);
}

std::coroutine_handle<>
thread_pool::executor_type::
dispatch(continuation& c) const
{
    if(pool_->impl_->running_in_this_thread())
        return c.h;
    pool_->impl_->post(c);
    return std::noop_coroutine();
}

} // capy
} // boost
