//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Minimal thread pool + executor for sender benchmarks.
//
// sender_thread_pool is the execution context.
// sender_executor is the lightweight handle obtained via
// get_executor(). Streams hold the executor, not the pool.
//

#ifndef BOOST_CAPY_BENCH_SENDER_THREAD_POOL_HPP
#define BOOST_CAPY_BENCH_SENDER_THREAD_POOL_HPP

#include "thread_pool.hpp"

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

class sender_thread_pool;

/// Lightweight executor handle referencing a sender_thread_pool.
struct sender_executor
{
    sender_thread_pool* pool_ = nullptr;

    void post(std::coroutine_handle<> h) const;
    void enqueue(work_item* w) const;

    auto dispatch(std::coroutine_handle<> h) const
        -> std::coroutine_handle<>;

    bool operator==(
        sender_executor const&) const noexcept = default;
};

class sender_thread_pool
{
    struct coro_work : work_item
    {
        std::coroutine_handle<> h_;

        explicit coro_work(std::coroutine_handle<> h) noexcept
            : h_(h) {}

        void execute() noexcept override
        {
            auto h = h_;
            delete this;
            h.resume();
        }
    };

    std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable done_cv_;
    intrusive_queue<work_item> q_;
    std::vector<std::thread> threads_;
    std::atomic<std::size_t> outstanding_work_{0};
    bool stop_{false};
    bool joined_{false};
    std::size_t num_threads_;
    std::once_flag start_flag_;

    void ensure_started()
    {
        std::call_once(start_flag_, [this] {
            threads_.reserve(num_threads_);
            for (std::size_t i = 0; i < num_threads_; ++i)
                threads_.emplace_back([this] { run(); });
        });
    }

    void run()
    {
        for (;;)
        {
            work_item* w = nullptr;
            {
                std::unique_lock lock(mutex_);
                work_cv_.wait(lock, [this] {
                    return !q_.empty() || stop_;
                });
                if (stop_)
                    return;
                w = q_.pop();
            }
            if (w)
                w->execute();
        }
    }

public:
    using executor_type = sender_executor;

    explicit sender_thread_pool(std::size_t num_threads = 0)
        : num_threads_(num_threads == 0
            ? (std::max)(std::thread::hardware_concurrency(), 1u)
            : num_threads)
    {}

    ~sender_thread_pool()
    {
        stop();
        join();
    }

    sender_thread_pool(sender_thread_pool const&) = delete;
    sender_thread_pool& operator=(sender_thread_pool const&) = delete;

    sender_executor get_executor() noexcept
    {
        return sender_executor{this};
    }

    void enqueue(work_item* w)
    {
        ensure_started();
        {
            std::lock_guard lock(mutex_);
            q_.push(w);
        }
        work_cv_.notify_one();
    }

    void post(std::coroutine_handle<> h)
    {
        enqueue(new coro_work(h));
    }

    void on_work_started() noexcept
    {
        outstanding_work_.fetch_add(1, std::memory_order_acq_rel);
    }

    void on_work_finished() noexcept
    {
        if (outstanding_work_.fetch_sub(
            1, std::memory_order_acq_rel) == 1)
        {
            std::lock_guard lock(mutex_);
            if (joined_ && !stop_)
                stop_ = true;
            done_cv_.notify_all();
            work_cv_.notify_all();
        }
    }

    void join() noexcept
    {
        {
            std::unique_lock lock(mutex_);
            if (joined_)
                return;
            joined_ = true;

            if (outstanding_work_.load(
                std::memory_order_acquire) == 0)
            {
                stop_ = true;
                work_cv_.notify_all();
            }
            else
            {
                done_cv_.wait(lock, [this] { return stop_; });
            }
        }

        for (auto& t : threads_)
            if (t.joinable())
                t.join();
    }

    void stop() noexcept
    {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        work_cv_.notify_all();
        done_cv_.notify_all();
    }
};

// sender_executor inline definitions (need sender_thread_pool complete)

inline void sender_executor::post(std::coroutine_handle<> h) const
{
    pool_->post(h);
}

inline void sender_executor::enqueue(work_item* w) const
{
    pool_->enqueue(w);
}

inline auto sender_executor::dispatch(std::coroutine_handle<> h) const
    -> std::coroutine_handle<>
{
    pool_->post(h);
    return std::noop_coroutine();
}

#endif
