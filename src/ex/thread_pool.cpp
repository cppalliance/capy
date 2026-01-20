//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/capy
//

#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/core/intrusive_queue.hpp>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace boost {
namespace capy {

//------------------------------------------------------------------------------

class thread_pool::impl
{
    struct work : intrusive_queue<work>::node
    {
        any_coro h_;

        explicit work(any_coro h) noexcept
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
    intrusive_queue<work> q_;
    std::vector<std::jthread> threads_;
    std::size_t num_threads_;
    std::once_flag start_flag_;

public:
    ~impl()
    {
        stop();
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
    post(any_coro h)
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
                threads_.emplace_back([this](std::stop_token st){ run(st); });
        });
    }

    void
    run(std::stop_token st)
    {
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
post(any_coro h) const
{
    pool_->impl_->post(h);
}

} // capy
} // boost
