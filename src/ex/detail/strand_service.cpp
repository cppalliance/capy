//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "src/ex/detail/strand_queue.hpp"
#include <boost/capy/ex/detail/strand_service.hpp>
#include <atomic>
#include <coroutine>
#include <mutex>
#include <thread>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

//----------------------------------------------------------

/** Implementation state for a strand.

    Each strand_impl provides serialization for coroutines
    dispatched through strands that share it.
*/
struct strand_impl
{
    std::mutex mutex_;
    strand_queue pending_;
    bool locked_ = false;
    std::atomic<std::thread::id> dispatch_thread_{};
    void* cached_frame_ = nullptr;
};

//----------------------------------------------------------

/** Invoker coroutine for strand dispatch.

    Uses custom allocator to recycle frame - one allocation
    per strand_impl lifetime, stored in trailer for recovery.
*/
struct strand_invoker
{
    struct promise_type
    {
        void* operator new(std::size_t n, strand_impl& impl)
        {
            constexpr auto A = alignof(strand_impl*);
            std::size_t padded = (n + A - 1) & ~(A - 1);
            std::size_t total = padded + sizeof(strand_impl*);

            void* p = impl.cached_frame_
                ? std::exchange(impl.cached_frame_, nullptr)
                : ::operator new(total);

            // Trailer lets delete recover impl
            *reinterpret_cast<strand_impl**>(
                static_cast<char*>(p) + padded) = &impl;
            return p;
        }

        void operator delete(void* p, std::size_t n) noexcept
        {
            constexpr auto A = alignof(strand_impl*);
            std::size_t padded = (n + A - 1) & ~(A - 1);

            auto* impl = *reinterpret_cast<strand_impl**>(
                static_cast<char*>(p) + padded);

            if (!impl->cached_frame_)
                impl->cached_frame_ = p;
            else
                ::operator delete(p);
        }

        strand_invoker get_return_object() noexcept
        { return {std::coroutine_handle<promise_type>::from_promise(*this)}; }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h_;
};

//----------------------------------------------------------

/** Concrete implementation of strand_service.

    Holds the fixed pool of strand_impl objects.
*/
class strand_service_impl : public strand_service
{
    static constexpr std::size_t num_impls = 211;

    strand_impl impls_[num_impls];
    std::size_t salt_ = 0;
    std::mutex mutex_;

public:
    explicit
    strand_service_impl(execution_context&)
    {
    }

    strand_impl*
    get_implementation() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t index = salt_++;
        index = index % num_impls;
        return &impls_[index];
    }

protected:
    void
    shutdown() override
    {
        for(std::size_t i = 0; i < num_impls; ++i)
        {
            std::lock_guard<std::mutex> lock(impls_[i].mutex_);
            impls_[i].locked_ = true;

            if(impls_[i].cached_frame_)
            {
                ::operator delete(impls_[i].cached_frame_);
                impls_[i].cached_frame_ = nullptr;
            }
        }
    }

private:
    static bool
    enqueue(strand_impl& impl, std::coroutine_handle<> h)
    {
        std::lock_guard<std::mutex> lock(impl.mutex_);
        impl.pending_.push(h);
        if(!impl.locked_)
        {
            impl.locked_ = true;
            return true;
        }
        return false;
    }

    static void
    dispatch_pending(strand_impl& impl)
    {
        strand_queue::taken_batch batch;
        {
            std::lock_guard<std::mutex> lock(impl.mutex_);
            batch = impl.pending_.take_all();
        }
        impl.pending_.dispatch_batch(batch);
    }

    static bool
    try_unlock(strand_impl& impl)
    {
        std::lock_guard<std::mutex> lock(impl.mutex_);
        if(impl.pending_.empty())
        {
            impl.locked_ = false;
            return true;
        }
        return false;
    }

    static void
    set_dispatch_thread(strand_impl& impl) noexcept
    {
        impl.dispatch_thread_.store(std::this_thread::get_id());
    }

    static void
    clear_dispatch_thread(strand_impl& impl) noexcept
    {
        impl.dispatch_thread_.store(std::thread::id{});
    }

    // Loops until queue empty (aggressive). Alternative: per-batch fairness
    // (repost after each batch to let other work run) - explore if starvation observed.
    static strand_invoker
    make_invoker(strand_impl& impl)
    {
        strand_impl* p = &impl;
        for(;;)
        {
            set_dispatch_thread(*p);
            dispatch_pending(*p);
            if(try_unlock(*p))
            {
                clear_dispatch_thread(*p);
                co_return;
            }
        }
    }

    friend class strand_service;
};

//----------------------------------------------------------

strand_service::
strand_service()
    : service()
{
}

strand_service::
~strand_service() = default;

bool
strand_service::
running_in_this_thread(strand_impl& impl) noexcept
{
    return impl.dispatch_thread_.load() == std::this_thread::get_id();
}

std::coroutine_handle<>
strand_service::
dispatch(strand_impl& impl, executor_ref ex, std::coroutine_handle<> h)
{
    if(running_in_this_thread(impl))
        return h;

    if(strand_service_impl::enqueue(impl, h))
        ex.post(strand_service_impl::make_invoker(impl).h_);
    return std::noop_coroutine();
}

void
strand_service::
post(strand_impl& impl, executor_ref ex, std::coroutine_handle<> h)
{
    if(strand_service_impl::enqueue(impl, h))
        ex.post(strand_service_impl::make_invoker(impl).h_);
}

strand_service&
get_strand_service(execution_context& ctx)
{
    return ctx.use_service<strand_service_impl>();
}

} // namespace detail
} // namespace capy
} // namespace boost
