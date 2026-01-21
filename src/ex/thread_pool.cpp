//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/capy
//

#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/detail/intrusive.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

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
    std::vector<std::thread> threads_;
    std::atomic<bool> stop_requested_{false};
    std::size_t num_threads_;
    std::once_flag start_flag_;

public:
    ~impl()
    {
        stop();
        for (auto& t : threads_)
            if (t.joinable())
                t.join();
        threads_.clear();

        while(auto* w = q_.pop())
            w->destroy();
    }

    explicit
    impl(std::size_t num_threads)
        : num_threads_(num_threads)
    {
        if(num_threads_ == 0)
            num_threads_ = std::thread::hardware_concurrency();
        if(num_threads_ == 0)
            num_threads_ = 1;
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
        stop_requested_.store(true);
        cv_.notify_all();
    }

private:
    void
    ensure_started()
    {
        std::call_once(start_flag_, [this]{
            threads_.reserve(num_threads_);
            for(std::size_t i = 0; i < num_threads_; ++i)
                threads_.emplace_back([this]{ run(); });
        });
    }

    void
    run()
    {
        for(;;)
        {
            work* w = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]{ return !q_.empty() || stop_requested_.load(); });
                if (stop_requested_.load() && q_.empty())
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
thread_pool(std::size_t num_threads)
    : impl_(new impl(num_threads))
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
