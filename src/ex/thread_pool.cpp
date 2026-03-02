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
#include <boost/capy/detail/intrusive.hpp>
#include <boost/capy/test/thread_name.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

/*
    Thread pool implementation using a shared work queue.

    Work items are coroutine handles wrapped in intrusive list nodes, stored
    in a single queue protected by a mutex. Worker threads wait on a
    condition_variable until work is available or stop is requested.

    Threads are started lazily on first post() via std::call_once to avoid
    spawning threads for pools that are constructed but never used. Each
    thread is named with a configurable prefix plus index for debugger
    visibility.

    Shutdown sequence: stop() sets the stop flag and notifies all threads,
    then the destructor joins threads and destroys any remaining queued
    work without executing it.
*/

namespace boost {
namespace capy {

//------------------------------------------------------------------------------

class thread_pool::impl
{
    struct work : detail::intrusive_queue<work>::node
    {
        std::coroutine_handle<> h_;

        explicit work(std::coroutine_handle<> h) noexcept
            : h_(h)
        {
        }

        void run()
        {
            auto h = h_;
            delete this;
            h.resume();
        }

        void destroy()
        {
            delete this;
        }
    };

    std::mutex mutex_;
    std::condition_variable cv_;
    detail::intrusive_queue<work> q_;
    std::vector<std::thread> threads_;
    std::atomic<bool> stop_{false};
    std::size_t num_threads_;
    char thread_name_prefix_[13]{};  // 12 chars max + null terminator
    std::once_flag start_flag_;

public:
    ~impl()
    {
        stop();
        for(auto& t : threads_)
            if(t.joinable())
                t.join();

        while(auto* w = q_.pop())
            w->destroy();
    }

    impl(std::size_t num_threads, std::string_view thread_name_prefix)
        : num_threads_(num_threads)
    {
        if(num_threads_ == 0)
            num_threads_ = std::thread::hardware_concurrency();
        if(num_threads_ == 0)
            num_threads_ = 1;

        // Truncate prefix to 12 chars, leaving room for up to 3-digit index.
        auto n = thread_name_prefix.copy(thread_name_prefix_, 12);
        thread_name_prefix_[n] = '\0';
    }

    void
    post(std::coroutine_handle<> h)
    {
        ensure_started();
        auto* w = new work(h);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            q_.push(w);
        }
        cv_.notify_one();
    }

    void
    stop() noexcept
    {
        stop_.store(true, std::memory_order_release);
        cv_.notify_all();
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

        for(;;)
        {
            work* w = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]{
                    return !q_.empty() ||
                        stop_.load(std::memory_order_acquire);
                });
                if(stop_.load(std::memory_order_acquire) && q_.empty())
                    return;
                w = q_.pop();
            }
            if(w)
                w->run();
        }
    }
};

//------------------------------------------------------------------------------

thread_pool::
~thread_pool()
{
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
post(std::coroutine_handle<> h) const
{
    pool_->impl_->post(h);
}

} // capy
} // boost
