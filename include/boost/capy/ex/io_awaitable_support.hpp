//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_IO_AWAITABLE_SUPPORT_HPP
#define BOOST_CAPY_EX_IO_AWAITABLE_SUPPORT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/this_coro.hpp>

#include <coroutine>
#include <cstddef>
#include <memory_resource>
#include <stop_token>
#include <type_traits>

namespace boost {
namespace capy {

/** CRTP mixin that adds I/O awaitable support to a promise type.

    Inherit from this class to enable these capabilities in your coroutine:

    1. **Frame allocation** — The mixin provides `operator new/delete` that
       use the thread-local frame allocator set by `run_async`.

    2. **Frame allocator storage** — The mixin stores the allocator pointer
       for propagation to child tasks.

    3. **Stop token storage** — The mixin stores the `std::stop_token`
       that was passed when your coroutine was awaited.

    4. **Stop token access** — Coroutine code can retrieve the token via
       `co_await this_coro::stop_token`.

    5. **Executor storage** — The mixin stores the `executor_ref`
       that this coroutine is bound to.

    6. **Executor access** — Coroutine code can retrieve the executor via
       `co_await this_coro::executor`.

    @tparam Derived The derived promise type (CRTP pattern).

    @par Basic Usage

    For coroutines that need to access their stop token or executor:

    @code
    struct my_task
    {
        struct promise_type : io_awaitable_support<promise_type>
        {
            my_task get_return_object();
            std::suspend_always initial_suspend() noexcept;
            std::suspend_always final_suspend() noexcept;
            void return_void();
            void unhandled_exception();
        };

        // ... awaitable interface ...
    };

    my_task example()
    {
        auto token = co_await this_coro::stop_token;
        auto ex = co_await this_coro::executor;
        // Use token and ex...
    }
    @endcode

    @par Custom Awaitable Transformation

    If your promise needs to transform awaitables (e.g., for affinity or
    logging), override `transform_awaitable` instead of `await_transform`:

    @code
    struct promise_type : io_awaitable_support<promise_type>
    {
        template<typename A>
        auto transform_awaitable(A&& a)
        {
            // Your custom transformation logic
            return std::forward<A>(a);
        }
    };
    @endcode

    The mixin's `await_transform` intercepts @ref this_coro::stop_token_tag and
    @ref this_coro::executor_tag, then delegates all other awaitables to your
    `transform_awaitable`.

    @par Making Your Coroutine an IoAwaitable

    The mixin handles the "inside the coroutine" part—accessing the token
    and executor. To receive these when your coroutine is awaited (satisfying
    @ref IoAwaitable), implement the `await_suspend` overload on your
    coroutine return type:

    @code
    struct my_task
    {
        struct promise_type : io_awaitable_support<promise_type> { ... };

        std::coroutine_handle<promise_type> h_;

        // IoAwaitable await_suspend receives and stores the token and executor
        coro await_suspend(coro cont, executor_ref ex, std::stop_token token)
        {
            h_.promise().set_stop_token(token);
            h_.promise().set_executor(ex);
            // ... rest of suspend logic ...
        }
    };
    @endcode

    @par Thread Safety
    The stop token and executor are stored during `await_suspend` and read
    during `co_await this_coro::stop_token` or `co_await this_coro::executor`.
    These occur on the same logical thread of execution, so no synchronization
    is required.

    @see this_coro::stop_token
    @see this_coro::executor
    @see IoAwaitable
*/
template<typename Derived>
class io_awaitable_support
{
    executor_ref executor_;
    std::stop_token stop_token_;
    std::pmr::memory_resource* alloc_ = nullptr;
    executor_ref caller_ex_;
    mutable coro cont_{nullptr};

public:
    //----------------------------------------------------------
    // Frame allocation support
    //----------------------------------------------------------

private:
    static constexpr std::size_t ptr_alignment = alignof(void*);

    static std::size_t
    aligned_offset(std::size_t n) noexcept
    {
        return (n + ptr_alignment - 1) & ~(ptr_alignment - 1);
    }

public:
    /** Allocate a coroutine frame.

        Uses the thread-local frame allocator set by run_async.
        Falls back to default memory resource if not set.
        Stores the allocator pointer at the end of each frame for
        correct deallocation even when TLS changes.
    */
    static void*
    operator new(std::size_t size)
    {
        auto* mr = current_frame_allocator();
        if(!mr)
            mr = std::pmr::get_default_resource();

        // Allocate extra space for memory_resource pointer
        std::size_t ptr_offset = aligned_offset(size);
        std::size_t total = ptr_offset + sizeof(std::pmr::memory_resource*);
        void* raw = mr->allocate(total, alignof(std::max_align_t));

        // Store the allocator pointer at the end
        auto* ptr_loc = reinterpret_cast<std::pmr::memory_resource**>(
            static_cast<char*>(raw) + ptr_offset);
        *ptr_loc = mr;

        return raw;
    }

    /** Deallocate a coroutine frame.

        Reads the allocator pointer stored at the end of the frame
        to ensure correct deallocation regardless of current TLS.
    */
    static void
    operator delete(void* ptr, std::size_t size)
    {
        // Read the allocator pointer from the end of the frame
        std::size_t ptr_offset = aligned_offset(size);
        auto* ptr_loc = reinterpret_cast<std::pmr::memory_resource**>(
            static_cast<char*>(ptr) + ptr_offset);
        auto* mr = *ptr_loc;

        std::size_t total = ptr_offset + sizeof(std::pmr::memory_resource*);
        mr->deallocate(ptr, total, alignof(std::max_align_t));
    }

    ~io_awaitable_support()
    {
        if (cont_)
            cont_.destroy();
    }

    /** Store a frame allocator for later retrieval.

        Call this from initial_suspend to capture the current
        TLS allocator for propagation to child tasks.

        @param alloc The allocator to store.
    */
    void
    set_frame_allocator(std::pmr::memory_resource* alloc) noexcept
    {
        alloc_ = alloc;
    }

    /** Return the stored frame allocator.

        @return The allocator, or nullptr if none was set.
    */
    std::pmr::memory_resource*
    frame_allocator() const noexcept
    {
        return alloc_;
    }

    //----------------------------------------------------------
    // Continuation support
    //----------------------------------------------------------

    /** Store continuation and caller's executor for completion dispatch.

        Call this from your coroutine type's `await_suspend` overload to
        set up the completion path. On completion, the coroutine will
        resume the continuation, dispatching through the caller's executor
        if it differs from this coroutine's executor.

        @param cont The continuation to resume on completion.
        @param caller_ex The caller's executor for completion dispatch.
    */
    void set_continuation(coro cont, executor_ref caller_ex) noexcept
    {
        cont_ = cont;
        caller_ex_ = caller_ex;
    }

    /** Return the handle to resume on completion with dispatch-awareness.

        If no continuation was set, returns `std::noop_coroutine()`.
        If the coroutine's executor matches the caller's executor, returns
        the continuation directly for symmetric transfer.
        Otherwise, dispatches through the caller's executor and returns
        `std::noop_coroutine()`.

        Call this from your `final_suspend` awaiter's `await_suspend`.

        @return A coroutine handle for symmetric transfer.
    */
    coro complete() const noexcept
    {
        if(!cont_)
            return std::noop_coroutine();
        if(executor_ == caller_ex_)
            return std::exchange(cont_, nullptr);
        caller_ex_.dispatch(std::exchange(cont_, nullptr));
        return std::noop_coroutine();
    }

    /** Store a stop token for later retrieval.

        Call this from your coroutine type's `await_suspend`
        overload to make the token available via
        `co_await this_coro::stop_token`.

        @param token The stop token to store.
    */
    void set_stop_token(std::stop_token token) noexcept
    {
        stop_token_ = token;
    }

    /** Return the stored stop token.

        @return The stop token, or a default-constructed token if none was set.
    */
    std::stop_token const& stop_token() const noexcept
    {
        return stop_token_;
    }

    /** Store an executor for later retrieval.

        Call this from your coroutine type's `await_suspend`
        overload to make the executor available via
        `co_await this_coro::executor`.

        @param ex The executor to store.
    */
    void set_executor(executor_ref ex) noexcept
    {
        executor_ = ex;
    }

    /** Return the stored executor.

        @return The executor, or a default-constructed executor_ref if none was set.
    */
    executor_ref executor() const noexcept
    {
        return executor_;
    }

    /** Transform an awaitable before co_await.

        Override this in your derived promise type to customize how
        awaitables are transformed. The default implementation passes
        the awaitable through unchanged.

        @param a The awaitable expression from `co_await a`.

        @return The transformed awaitable.
    */
    template<typename A>
    decltype(auto) transform_awaitable(A&& a)
    {
        return std::forward<A>(a);
    }

    /** Intercept co_await expressions.

        This function handles @ref this_coro::stop_token_tag and
        @ref this_coro::executor_tag specially, returning an awaiter that
        yields the stored value. All other awaitables are delegated to
        @ref transform_awaitable.

        @param t The awaited expression.

        @return An awaiter for the expression.
    */
    template<typename T>
    auto await_transform(T&& t)
    {
        if constexpr (std::is_same_v<std::decay_t<T>, this_coro::stop_token_tag>)
        {
            struct awaiter
            {
                std::stop_token token_;

                bool await_ready() const noexcept
                {
                    return true;
                }

                void await_suspend(coro) const noexcept
                {
                }

                std::stop_token await_resume() const noexcept
                {
                    return token_;
                }
            };
            return awaiter{stop_token_};
        }
        else if constexpr (std::is_same_v<std::decay_t<T>, this_coro::executor_tag>)
        {
            struct awaiter
            {
                executor_ref executor_;

                bool await_ready() const noexcept
                {
                    return true;
                }

                void await_suspend(coro) const noexcept
                {
                }

                executor_ref await_resume() const noexcept
                {
                    return executor_;
                }
            };
            return awaiter{executor_};
        }
        else
        {
            return static_cast<Derived*>(this)->transform_awaitable(
                std::forward<T>(t));
        }
    }
};

} // namespace capy
} // namespace boost

#endif
