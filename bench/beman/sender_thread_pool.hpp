//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Minimal thread pool for sender benchmarks.
//
// sender_thread_pool is the execution context.
// pool_scheduler (defined in sender_io_env.hpp)
// is the P2300 scheduler handle.
//

#ifndef BOOST_CAPY_BENCH_SENDER_THREAD_POOL_HPP
#define BOOST_CAPY_BENCH_SENDER_THREAD_POOL_HPP

#include "thread_pool.hpp"

#include <boost/capy/ex/execution_context.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

struct pool_scheduler;

class sender_thread_pool
    : public boost::capy::execution_context
{
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
    explicit sender_thread_pool(std::size_t num_threads = 0)
        : execution_context(this)
        , num_threads_(num_threads == 0
            ? (std::max)(std::thread::hardware_concurrency(), 1u)
            : num_threads)
    {}

    ~sender_thread_pool()
    {
        stop();
        join();
        shutdown();
        destroy();
    }

    sender_thread_pool(sender_thread_pool const&) = delete;
    sender_thread_pool& operator=(sender_thread_pool const&) = delete;

    // Defined in sender_io_env.hpp after pool_scheduler
    pool_scheduler get_scheduler() noexcept;

    void enqueue(work_item* w)
    {
        ensure_started();
        {
            std::lock_guard lock(mutex_);
            q_.push(w);
        }
        work_cv_.notify_one();
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

#endif
