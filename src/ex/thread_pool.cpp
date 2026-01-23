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
#include <boost/capy/detail/thread_name.hpp>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

/*
    Thread pool implementation using a shared work queue.

    Work items are coroutine handles wrapped in intrusive list nodes, stored
    in a single queue protected by a mutex. Worker threads wait on a
    condition_variable_any that integrates with std::stop_token for clean
    shutdown.

    Threads are started lazily on first post() via std::call_once to avoid
    spawning threads for pools that are constructed but never used. Each
    thread is named with a configurable prefix plus index for debugger
    visibility.

    Shutdown sequence: stop() requests all threads to stop via their stop
    tokens, then the destructor joins threads and destroys any remaining
    queued work without executing it.
*/

namespace boost {
namespace capy {

//------------------------------------------------------------------------------

class thread_pool::impl
{
    struct work : detail::intrusive_queue<work>::node
    {
        coro h_;

        explicit work(coro h) noexcept
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
    std::condition_variable_any cv_;
    detail::intrusive_queue<work> q_;
    std::vector<std::jthread> threads_;
    std::size_t num_threads_;
    char thread_name_prefix_[13]{};  // 12 chars max + null terminator
    std::once_flag start_flag_;

public:
    ~impl()
    {
        stop();
        threads_.clear();

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
    post(coro h)
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
        for (auto& t : threads_)
            t.request_stop();
        cv_.notify_all();
    }

private:
    void
    ensure_started()
    {
        std::call_once(start_flag_, [this]{
            threads_.reserve(num_threads_);
            for(std::size_t i = 0; i < num_threads_; ++i)
                threads_.emplace_back(
                    [this, i](std::stop_token st){ run(st, i); });
        });
    }

    void
    run(std::stop_token st, std::size_t index)
    {
        // Build name; set_current_thread_name truncates to platform limits.
        char name[16];
        std::snprintf(name, sizeof(name), "%s%zu", thread_name_prefix_, index);
        detail::set_current_thread_name(name);

        for(;;)
        {
            work* w = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if(!cv_.wait(lock, st, [this]{ return !q_.empty(); }))
                    return;
                w = q_.pop();
            }
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
}

void
thread_pool::
stop() noexcept
{
    impl_->stop();
}

//------------------------------------------------------------------------------

void
thread_pool::executor_type::
post(coro h) const
{
    pool_->impl_->post(h);
}

} // capy
} // boost
