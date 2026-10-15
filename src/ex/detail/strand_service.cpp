//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "src/ex/detail/strand_impl.hpp"
#include <boost/capy/ex/detail/strand_service.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/detail/config.hpp>
#include <coroutine>
#include <memory>
#include <tuple>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

// Sentinel stored in invoker_frame_cache_ after shutdown to prevent
// in-flight invokers from repopulating a freed cache slot.
inline void* const kCacheClosed = reinterpret_cast<void*>(1);

/** Concrete strand_service.

    Holds a shared mutex pool (193 entries), a linked list of live
    impls (for shutdown traversal), and a single-slot invoker
    coroutine frame cache shared across all strands of this service.

    The dispatch helpers (`enqueue`, `dispatch_pending`, etc.) are
    public so the namespace-scope `make_invoker` coroutine and the
    `strand_service` static methods can call them without friendship.
*/
class strand_service_impl : public strand_service
{
public:
    static constexpr std::size_t num_mutexes = 193;

    std::mutex mutex_;
    std::size_t salt_ = 0;
    std::shared_ptr<std::mutex> mutexes_[num_mutexes];
    intrusive_list<strand_impl> impl_list_;
    std::atomic<void*> invoker_frame_cache_{nullptr};
    bool shutdown_ = false;

    explicit
    strand_service_impl(execution_context&)
    {
    }

    std::shared_ptr<strand_impl>
    create_implementation() override
    {
        auto new_impl = std::make_shared<strand_impl>();

        std::lock_guard<std::mutex> lock(mutex_);
        if(shutdown_)
        {
            // Born closed, like the strands shutdown() closed: it
            // borrows nothing from this service.
            new_impl->locked_ = true;
            return new_impl;
        }

        std::size_t s = salt_++;
        std::size_t idx = reinterpret_cast<std::size_t>(new_impl.get());
        idx += idx >> 3;
        idx ^= s + 0x9e3779b9 + (idx << 6) + (idx >> 2);
        idx %= num_mutexes;
        if(!mutexes_[idx])
            mutexes_[idx] = std::make_shared<std::mutex>();
        new_impl->mutex_ = mutexes_[idx].get();

        impl_list_.push_back(new_impl.get());
        new_impl->service_.store(this, std::memory_order_release);

        return new_impl;
    }

    // Returns the service when the caller must post an invoker.
    static strand_service_impl*
    enqueue(strand_impl& impl, continuation& c)
    {
        // A closed strand never locks: the mutex it borrowed may have
        // gone with its service.
        if(impl.service_.load(std::memory_order_acquire))
        {
            std::lock_guard<std::mutex> lock(*impl.mutex_);
            if(auto* svc = impl.service_.load(std::memory_order_relaxed))
            {
                impl.pending_.push(c);
                if(!impl.locked_)
                {
                    impl.locked_ = true;
                    return svc;
                }
                return nullptr;
            }
        }
        // Shut down: nothing will run it, so abandon it as its holder.
        capy::abandon(c);
        return nullptr;
    }

    enum class withdrawal
    {
        alone,
        followed,
        taken
    };

    // Takes back a continuation whose invoker could not be posted.
    // `taken` means a shutdown took it first and abandoned it.
    static withdrawal
    withdraw(strand_impl& impl, continuation& c) noexcept
    {
        std::lock_guard<std::mutex> lock(*impl.mutex_);
        if(!impl.service_.load(std::memory_order_relaxed))
            return withdrawal::taken;
        // Nothing takes from the queue while it waits for an invoker,
        // so `c` is still first.
        BOOST_CAPY_ASSERT(impl.pending_.front() == &c);
        std::ignore = c;
        impl.pending_.pop_front();
        // Posts queued behind `c` returned trusting its invoker, so
        // the strand stays locked until one runs them.
        if(!impl.pending_.empty())
            return withdrawal::followed;
        impl.locked_ = false;
        return withdrawal::alone;
    }

    // Gives up queued work that no invoker can be posted to run.
    static void
    abandon_pending(strand_impl& impl) noexcept
    {
        strand_queue::taken_batch batch;
        {
            std::lock_guard<std::mutex> lock(*impl.mutex_);
            if(!impl.service_.load(std::memory_order_relaxed))
                return;
            batch = impl.pending_.take_all();
            impl.locked_ = false;
        }
        strand_queue::abandon_batch(batch);
    }

    static void
    dispatch_pending(strand_impl& impl)
    {
        strand_queue::taken_batch batch;
        {
            std::lock_guard<std::mutex> lock(*impl.mutex_);
            batch = impl.pending_.take_all();
        }
        impl.pending_.dispatch_batch(batch);
    }

    static bool
    try_unlock(strand_impl& impl)
    {
        std::lock_guard<std::mutex> lock(*impl.mutex_);
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

    // Defined below; needs strand_invoker complete.
    static void
    post_invoker(
        strand_service_impl* svc,
        std::shared_ptr<strand_impl> impl,
        executor_ref ex);

protected:
    void
    shutdown() override
    {
        strand_queue::taken_batch doomed;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
            while(auto* p = impl_list_.pop_front())
            {
                std::lock_guard<std::mutex> impl_lock(*p->mutex_);
                p->locked_ = true;
                p->service_.store(nullptr, std::memory_order_release);
                auto b = p->pending_.take_all();
                if(b.head)
                {
                    if(doomed.tail)
                        doomed.tail->reserved = b.head;
                    else
                        doomed.head = b.head;
                    doomed.tail = b.tail;
                }
            }
        }
        // Outside the mutexes: a destroyed frame may drop the last
        // strand handle and re-enter the service.
        strand_queue::abandon_batch(doomed);

        void* fp = invoker_frame_cache_.exchange(
            kCacheClosed, std::memory_order_acq_rel);
        if(fp) ::operator delete(fp);
    }
};

/** Invoker coroutine that drains a strand's pending queue.

    Runs once the strand transitions from unlocked to locked. Holds
    the impl alive via the coroutine parameter (a shared_ptr in the
    coroutine frame), so user code may drop its strand handle while
    the invoker is mid-flight.

    The frame's allocator recycles a single per-service slot. The
    trailer points at the service (lifetime: execution_context),
    NOT the impl (lifetime: per-strand), so operator delete is
    safe even after the impl has been destroyed.
*/
struct strand_invoker
{
    struct promise_type
    {
        // Stored in the coroutine frame so its address is stable for
        // posting to the inner executor.
        continuation self_;

        // The service is the one enqueue() saw under the strand's lock;
        // the impl's own pointer may be cleared by a concurrent shutdown.
        void*
        operator new(
            std::size_t n,
            strand_service_impl* svc,
            std::shared_ptr<strand_impl> const&)
        {
            constexpr auto A = alignof(strand_service_impl*);
            std::size_t padded = (n + A - 1) & ~(A - 1);
            std::size_t total = padded + sizeof(strand_service_impl*);

            // Never takes the closed marker, which would let a later
            // delete park a frame that nothing frees.
            void* p = svc->invoker_frame_cache_.load(
                std::memory_order_acquire);
            while(p && p != kCacheClosed &&
                !svc->invoker_frame_cache_.compare_exchange_weak(
                    p, nullptr, std::memory_order_acquire))
            {
            }
            if(!p || p == kCacheClosed)
                p = ::operator new(total);

            *reinterpret_cast<strand_service_impl**>(
                static_cast<char*>(p) + padded) = svc;
            return p;
        }

        void
        operator delete(void* p, std::size_t n) noexcept
        {
            constexpr auto A = alignof(strand_service_impl*);
            std::size_t padded = (n + A - 1) & ~(A - 1);
            auto* svc = *reinterpret_cast<strand_service_impl**>(
                static_cast<char*>(p) + padded);

            void* expected = nullptr;
            if(!svc->invoker_frame_cache_.compare_exchange_strong(
                    expected, p, std::memory_order_release))
                ::operator delete(p);
        }

        strand_invoker
        get_return_object() noexcept
        {
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_never  final_suspend()   noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); } // LCOV_EXCL_LINE terminates the process
    };

    std::coroutine_handle<promise_type> h_;
};

// The by-value parameter lives in the coroutine frame for the
// invoker's lifetime, keeping the impl alive past any user-side
// strand drop.
static
strand_invoker
make_invoker(strand_service_impl*, std::shared_ptr<strand_impl> impl)
{
    auto* p = impl.get();
    for(;;)
    {
        strand_service_impl::set_dispatch_thread(*p);
        strand_service_impl::dispatch_pending(*p);
        if(strand_service_impl::try_unlock(*p))
        {
            strand_service_impl::clear_dispatch_thread(*p);
            co_return;
        }
    }
}

void
strand_service_impl::post_invoker(
    strand_service_impl* svc,
    std::shared_ptr<strand_impl> impl,
    executor_ref ex)
{
    auto invoker = make_invoker(svc, std::move(impl));
    auto& self = invoker.h_.promise().self_;
    self.h = invoker.h_;
    try
    {
        ex.post(self);
    }
    catch(...)
    {
        invoker.h_.destroy();
        throw;
    }
}

// A post that throws leaves the caller holding `c`, unless a
// shutdown already took and abandoned it.
static
void
post_or_withdraw(
    strand_service_impl* svc,
    std::shared_ptr<strand_impl> const& impl,
    executor_ref ex,
    continuation& c)
{
    try
    {
        strand_service_impl::post_invoker(svc, impl, ex);
    }
    catch(...)
    {
        using withdrawal = strand_service_impl::withdrawal;
        auto const w = strand_service_impl::withdraw(*impl, c);
        if(w == withdrawal::taken)
            return;
        if(w == withdrawal::followed)
        {
            try
            {
                strand_service_impl::post_invoker(svc, impl, ex);
            }
            catch(...)
            {
                strand_service_impl::abandon_pending(*impl);
            }
        }
        throw;
    }
}

strand_impl::~strand_impl()
{
    if(auto* svc = service_.load(std::memory_order_acquire))
    {
        std::lock_guard<std::mutex> lock(svc->mutex_);
        svc->impl_list_.remove(this);
    }
    // Nothing can run these any more: give them up.
    auto batch = pending_.take_all();
    strand_queue::abandon_batch(batch);
}

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
dispatch(
    std::shared_ptr<strand_impl> const& impl,
    executor_ref ex,
    continuation& c)
{
    if(running_in_this_thread(*impl))
        return c.h;

    if(auto* svc = strand_service_impl::enqueue(*impl, c))
        post_or_withdraw(svc, impl, ex, c);
    return std::noop_coroutine();
}

void
strand_service::
post(
    std::shared_ptr<strand_impl> const& impl,
    executor_ref ex,
    continuation& c)
{
    if(auto* svc = strand_service_impl::enqueue(*impl, c))
        post_or_withdraw(svc, impl, ex, c);
}

strand_service&
get_strand_service(execution_context& ctx)
{
    return ctx.use_service<strand_service_impl>();
}

} // namespace detail
} // namespace capy
} // namespace boost
