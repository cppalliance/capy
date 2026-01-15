//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/capy
//

#include <boost/capy/ex/thread_pool.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace boost {
namespace capy {

//------------------------------------------------------------------------------

// Handler that wraps an any_coro for execution
class coro_handler : public execution_context::handler
{
    any_coro h_;

public:
    explicit coro_handler(any_coro h) noexcept
        : h_(h)
    {
    }

    void operator()() override
    {
        auto h = h_;
        delete this;
        h.resume();
    }

    void destroy() override
    {
        delete this;
    }
};

//------------------------------------------------------------------------------

class thread_pool::impl
{
    std::mutex mutex_;
    std::condition_variable cv_;
    execution_context::queue q_;
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

        // Remaining handlers destroyed by queue destructor
    }

    explicit
    impl(std::size_t num_threads)
        : stop_(false)
    {
        if(num_threads == 0)
            num_threads = std::thread::hardware_concurrency();
        if(num_threads == 0)
            num_threads = 1;

        threads_.reserve(num_threads);
        for(std::size_t i = 0; i < num_threads; ++i)
            threads_.emplace_back([this]{ run(); });
    }

    void
    post(any_coro h)
    {
        auto* handler = new coro_handler(h);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            q_.push(handler);
        }
        cv_.notify_one();
    }

private:
    void
    run()
    {
        for(;;)
        {
            execution_context::handler* h = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]{
                    return stop_ || !q_.empty();
                });

                if(stop_ && q_.empty())
                    return;

                h = q_.pop();
            }

            (*h)();
        }
    }
};

//------------------------------------------------------------------------------

thread_pool::
~thread_pool()
{
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
