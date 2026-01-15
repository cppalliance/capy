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
#include <thread>
#include <vector>

namespace boost {
namespace capy {

//------------------------------------------------------------------------------

// Pimpl implementation hides threading details from the header
class thread_pool::impl
{
    // Wraps a coroutine handle for queue storage
    struct work : intrusive_queue<work>::node
    {
        any_coro h_;

        explicit work(any_coro h) noexcept
            : h_(h)
        {
        }

        void run()
        {
            // delete before dispatch
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
    intrusive_queue<work> q_;
    std::vector<std::thread> threads_;
    bool stop_;

public:
    ~impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();

        for(auto& t : threads_)
            t.join();

        // Destroy any work items that were never executed
        while(auto* w = q_.pop())
            w->destroy();
    }

    explicit
    impl(std::size_t num_threads)
        : stop_(false)
    {
        if( num_threads == 0)
            num_threads = std::thread::hardware_concurrency();
        // Fallback
        if( num_threads == 0)
            num_threads = 1;

        threads_.reserve(num_threads);
        for(std::size_t i = 0; i < num_threads; ++i)
            threads_.emplace_back([this]{ run(); });
    }

    void
    post(any_coro h)
    {
        auto* w = new work(h);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            q_.push(w);
        }
        cv_.notify_one();
    }

private:
    void
    run()
    {
        for(;;)
        {
            work* w = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]{
                    return stop_ || !q_.empty();
                });

                // Only exit when stopped AND queue is drained
                if(stop_ && q_.empty())
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
    // Order matters: shutdown services, then impl, then base
    shutdown();
    delete impl_;
    destroy();
}

thread_pool::
thread_pool(std::size_t num_threads)
    : impl_(new impl(num_threads))
{
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
