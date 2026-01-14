//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/capy
//

#include "src/work_allocator.hpp"

#include <boost/capy/ex/thread_pool.hpp>
#include <atomic>
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
        h_.resume();
    }

    void destroy() override
    {
        // Coroutine handle is not owned, nothing to destroy
    }
};

//------------------------------------------------------------------------------

class thread_pool::impl
{
    // Prepended to each work allocation to track metadata
    struct header
    {
        header* next;
        std::size_t size;
        std::size_t align;
    };

    std::mutex mutex_;
    std::condition_variable cv_;
    header* head_;
    header* tail_;
    std::vector<std::thread> threads_;
    work_allocator arena_;
    std::atomic<std::size_t> work_count_;
    bool stop_;

    static header*
    to_header(void* p) noexcept
    {
        return static_cast<header*>(p) - 1;
    }

    static void*
    from_header(header* h) noexcept
    {
        return h + 1;
    }

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

        // Drain remaining work (no lock needed, threads are joined)
        while(head_)
        {
            header* h = head_;
            head_ = head_->next;
            auto* w = static_cast<execution_context::handler*>(from_header(h));
            w->destroy();
            arena_.deallocate(h, h->size, h->align);
        }
    }

    explicit
    impl(std::size_t num_threads)
        : head_(nullptr)
        , tail_(nullptr)
        , work_count_(0)
        , stop_(false)
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
    on_work_started() noexcept
    {
        ++work_count_;
    }

    void
    on_work_finished() noexcept
    {
        --work_count_;
    }

    void*
    allocate(std::size_t size, std::size_t align)
    {
        // Allocate space for header + work object
        std::size_t total = sizeof(header) + size;
        std::lock_guard<std::mutex> lock(mutex_);
        void* p = arena_.allocate(total, align);
        auto* h = new(p) header{nullptr, total, align};
        return from_header(h);
    }

    void
    deallocate(void* p, std::size_t, std::size_t) noexcept
    {
        // Size/align from caller are ignored; we use stored values
        header* h = to_header(p);
        std::lock_guard<std::mutex> lock(mutex_);
        arena_.deallocate(h, h->size, h->align);
    }

    void
    submit(execution_context::handler* h)
    {
        header* hdr = to_header(h);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hdr->next = nullptr;
            if(tail_)
                tail_->next = hdr;
            else
                head_ = hdr;
            tail_ = hdr;
        }
        cv_.notify_one();
    }

    void
    post(any_coro h)
    {
        // Allocate handler and submit
        void* p = allocate(sizeof(coro_handler), alignof(coro_handler));
        auto* handler = new(p) coro_handler(h);
        submit(handler);
    }

private:
    void
    run()
    {
        for(;;)
        {
            header* h = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]{
                    return stop_ || head_ != nullptr;
                });

                if(stop_ && !head_)
                    return;

                h = head_;
                head_ = head_->next;
                if(!head_)
                    tail_ = nullptr;
            }

            auto* w = static_cast<execution_context::handler*>(from_header(h));
            (*w)();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                arena_.deallocate(h, h->size, h->align);
            }
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
post(any_coro h) const
{
    pool_->impl_->post(h);
}

} // capy
} // boost
