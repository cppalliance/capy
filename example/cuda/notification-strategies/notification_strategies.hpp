//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EXAMPLE_CUDA_NOTIFICATION_STRATEGIES_HPP
#define BOOST_CAPY_EXAMPLE_CUDA_NOTIFICATION_STRATEGIES_HPP

#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>

#include <cuda_runtime.h>

#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace boost {
namespace capy {
namespace example {

/// Error category mapping `cudaError_t` values to messages.
class cuda_error_category
    : public std::error_category
{
public:
    char const* name() const noexcept override
    {
        return "cuda";
    }

    std::string message(int ev) const override
    {
        return ::cudaGetErrorString(static_cast<cudaError_t>(ev));
    }
};

/// Return the singleton CUDA error category.
inline std::error_category const& cuda_category() noexcept
{
    static cuda_error_category const cat;
    return cat;
}

/// Convert a `cudaError_t` to a `std::error_code`.
inline std::error_code make_cuda_error(cudaError_t e) noexcept
{
    return std::error_code(static_cast<int>(e), cuda_category());
}

/// One pending poll registration. `cont` and `ec` point into the
/// awaitable's coroutine frame, valid for the suspension's lifetime.
struct poll_entry
{
    cudaEvent_t event;
    executor_ref ex;
    continuation* cont;
    std::error_code* ec;
};

/// A dedicated thread that polls CUDA events with `cudaEventQuery` and
/// posts each waiter's continuation once its event reports ready.
///
/// This is the notification mechanism that avoids both blocking a worker
/// thread and registering a driver callback. Must be destroyed before
/// the executor it posts to; in this example no waits are outstanding at
/// teardown because the driver joins every pipeline first.
class poll_service
{
    std::mutex mtx_;
    std::vector<poll_entry> entries_;
    std::jthread thread_;

    void run(std::stop_token st)
    {
        while(! st.stop_requested())
        {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                for(std::size_t i = 0; i < entries_.size();)
                {
                    auto& e = entries_[i];
                    auto s = cudaEventQuery(e.event);
                    if(s == cudaErrorNotReady)
                    {
                        ++i;
                        continue;
                    }
                    *e.ec = (s == cudaSuccess)
                        ? std::error_code{}
                        : make_cuda_error(s);
                    e.ex.post(*e.cont);
                    entries_[i] = entries_.back();
                    entries_.pop_back();
                }
            }
            std::this_thread::yield();
        }
    }

public:
    poll_service()
        : thread_([this](std::stop_token st) { run(st); })
    {
    }

    /// Register a waiter; its continuation posts when `e.event` is ready.
    void register_wait(poll_entry e)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        entries_.push_back(e);
    }
};

/// A single-thread worker that runs blocking jobs off the calling
/// thread. The deferred-sync mechanism uses it to call the blocking
/// `cudaStreamSynchronize` without stalling a worker thread, then posts
/// the continuation. Stops and joins on destruction.
class sync_service
{
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> jobs_;
    bool stop_ = false;
    std::jthread thread_;

    void run()
    {
        for(;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mtx_);
                cv_.wait(lock, [this] { return stop_ || ! jobs_.empty(); });
                if(stop_ && jobs_.empty())
                    return;
                job = std::move(jobs_.front());
                jobs_.pop();
            }
            job();
        }
    }

public:
    sync_service()
        : thread_([this] { run(); })
    {
    }

    ~sync_service()
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
    }

    /// Enqueue a job to run on the service thread.
    void post(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            jobs_.push(std::move(job));
        }
        cv_.notify_one();
    }
};

struct callback_awaitable;
class poll_awaitable;
class deferred_sync_awaitable;

/// Owns a CUDA stream; created on construction, destroyed on teardown.
class cuda_stream
{
    cudaStream_t stream_ = nullptr;

public:
    cuda_stream()
    {
        auto err = cudaStreamCreate(&stream_);
        if(err != cudaSuccess)
            throw std::system_error(make_cuda_error(err));
    }

    ~cuda_stream()
    {
        if(stream_)
            cudaStreamDestroy(stream_);
    }

    cuda_stream(cuda_stream&& other) noexcept
        : stream_(std::exchange(other.stream_, nullptr))
    {
    }

    cuda_stream& operator=(cuda_stream&& other) noexcept
    {
        if(this != &other)
        {
            if(stream_)
                cudaStreamDestroy(stream_);
            stream_ = std::exchange(other.stream_, nullptr);
        }
        return *this;
    }

    cuda_stream(cuda_stream const&) = delete;
    cuda_stream& operator=(cuda_stream const&) = delete;

    /// Return the underlying CUDA stream handle.
    cudaStream_t native_handle() const noexcept
    {
        return stream_;
    }

    /// Return an IoAwaitable that completes via a host-function callback.
    callback_awaitable sync_via_callback() noexcept;

    /// Return an IoAwaitable that completes after a blocking synchronize
    /// runs on `svc`.
    deferred_sync_awaitable sync_via_deferred(sync_service& svc) noexcept;
};

/// Owns a CUDA event used to observe stream progress.
class cuda_event
{
    cudaEvent_t event_ = nullptr;

public:
    cuda_event()
    {
        auto err = cudaEventCreateWithFlags(&event_, cudaEventDisableTiming);
        if(err != cudaSuccess)
            throw std::system_error(make_cuda_error(err));
    }

    ~cuda_event()
    {
        if(event_)
            cudaEventDestroy(event_);
    }

    cuda_event(cuda_event&& other) noexcept
        : event_(std::exchange(other.event_, nullptr))
    {
    }

    cuda_event& operator=(cuda_event&& other) noexcept
    {
        if(this != &other)
        {
            if(event_)
                cudaEventDestroy(event_);
            event_ = std::exchange(other.event_, nullptr);
        }
        return *this;
    }

    cuda_event(cuda_event const&) = delete;
    cuda_event& operator=(cuda_event const&) = delete;

    /// Return the underlying CUDA event handle.
    cudaEvent_t native_handle() const noexcept
    {
        return event_;
    }

    /// Record this event into `stream` at the current queue position.
    void record(cudaStream_t stream)
    {
        auto err = cudaEventRecord(event_, stream);
        if(err != cudaSuccess)
            throw std::system_error(make_cuda_error(err));
    }

    /// Return an IoAwaitable that completes when this event is observed
    /// ready by `svc`.
    poll_awaitable sync_via_poll(poll_service& svc) noexcept;
};

/** IoAwaitable that resumes via a CUDA host-function callback.

    `cudaLaunchHostFunc` enqueues a host callback at the stream's current
    position. CUDA runs it on a driver-owned thread that must not make
    CUDA calls or resume the coroutine inline, so the callback posts the
    continuation through the captured executor instead. One operation is
    in flight at a time, so the resume context is a member rather than a
    per-operation allocation.

    The host function receives no completion status, so `await_resume`
    queries the stream after resumption to report a fault that occurred
    before the callback ran.
*/
struct callback_awaitable
{
    cudaStream_t stream;

    struct resume_ctx
    {
        executor_ref ex;
        continuation* cont = nullptr;
        std::error_code* ec = nullptr;
    };

    continuation cont_;
    std::error_code ec_;
    resume_ctx ctx_;

    static void CUDART_CB
    on_complete(void* arg)
    {
        auto* c = static_cast<resume_ctx*>(arg);
        *c->ec = std::error_code{};
        c->ex.post(*c->cont);
    }

    bool await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, io_env const* env)
    {
        cont_.h = h;
        ctx_ = resume_ctx{env->executor, &cont_, &ec_};
        auto err = cudaLaunchHostFunc(stream, &on_complete, &ctx_);
        if(err != cudaSuccess)
        {
            // Could not register: resume inline so we never deadlock.
            ec_ = make_cuda_error(err);
            return h;
        }
        return std::noop_coroutine();
    }

    // cudaLaunchHostFunc hands the host function no completion status,
    // so a stream fault is invisible from inside on_complete. Back on the
    // worker thread, CUDA calls are permitted again and cudaStreamQuery
    // exposes the stream's sticky error.
    std::error_code await_resume() const noexcept
    {
        if(ec_)
            return ec_;
        auto s = cudaStreamQuery(stream);
        if(s == cudaSuccess || s == cudaErrorNotReady)
            return {};
        return make_cuda_error(s);
    }
};

inline callback_awaitable
cuda_stream::sync_via_callback() noexcept
{
    return callback_awaitable{stream_};
}

/// IoAwaitable that resumes when a CUDA event reports ready, detected by
/// a polling service thread rather than a callback or a blocking wait.
///
/// @note A production awaitable should also check `env->stop_token` for
///     cancellation in `await_suspend`; these demo awaitables omit that
///     for brevity.
class poll_awaitable
{
    poll_service& svc_;
    cudaEvent_t event_;
    continuation cont_;
    std::error_code ec_;

public:
    poll_awaitable(poll_service& svc, cudaEvent_t event) noexcept
        : svc_(svc)
        , event_(event)
    {
    }

    bool await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, io_env const* env)
    {
        cont_.h = h;
        svc_.register_wait(poll_entry{event_, env->executor, &cont_, &ec_});
        return std::noop_coroutine();
    }

    std::error_code await_resume() const noexcept
    {
        return ec_;
    }
};

inline poll_awaitable
cuda_event::sync_via_poll(poll_service& svc) noexcept
{
    return poll_awaitable{svc, event_};
}

/// IoAwaitable that resumes after a blocking `cudaStreamSynchronize` runs
/// on a service thread, keeping the worker thread free meanwhile.
class deferred_sync_awaitable
{
    sync_service& svc_;
    cudaStream_t stream_;
    continuation cont_;
    std::error_code ec_;

public:
    deferred_sync_awaitable(sync_service& svc, cudaStream_t stream) noexcept
        : svc_(svc)
        , stream_(stream)
    {
    }

    bool await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, io_env const* env)
    {
        auto ex = env->executor;
        auto stream = stream_;
        cont_.h = h;
        svc_.post([ex, stream, ec = &ec_, cont = &cont_]() mutable
        {
            auto err = cudaStreamSynchronize(stream);
            *ec = (err == cudaSuccess)
                ? std::error_code{}
                : make_cuda_error(err);
            ex.post(*cont);
        });
        return std::noop_coroutine();
    }

    std::error_code await_resume() const noexcept
    {
        return ec_;
    }
};

inline deferred_sync_awaitable
cuda_stream::sync_via_deferred(sync_service& svc) noexcept
{
    return deferred_sync_awaitable{svc, stream_};
}

} // namespace example
} // namespace capy
} // namespace boost

#endif
