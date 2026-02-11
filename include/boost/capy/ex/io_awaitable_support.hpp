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
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/io_env.hpp>
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

    2. **Environment storage** — The mixin stores a pointer to the `io_env`
       containing the executor, stop token, and allocator for this coroutine.

    3. **Environment access** — Coroutine code can retrieve the environment
       via `co_await this_coro::environment`, or individual fields via
       `co_await this_coro::executor`, `co_await this_coro::stop_token`,
       and `co_await this_coro::allocator`.

    @tparam Derived The derived promise type (CRTP pattern).

    @par Basic Usage

    For coroutines that need to access their execution environment:

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
        auto env = co_await this_coro::environment;
        // Access env->executor, env->stop_token, env->allocator

        // Or use fine-grained accessors:
        auto ex = co_await this_coro::executor;
        auto token = co_await this_coro::stop_token;
        auto* alloc = co_await this_coro::allocator;
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

    The mixin's `await_transform` intercepts @ref this_coro::environment_tag
    and the fine-grained tag types (@ref this_coro::executor_tag,
    @ref this_coro::stop_token_tag, @ref this_coro::allocator_tag),
    then delegates all other awaitables to your `transform_awaitable`.

    @par Making Your Coroutine an IoAwaitable

    The mixin handles the "inside the coroutine" part—accessing the
    environment. To receive the environment when your coroutine is awaited
    (satisfying @ref IoAwaitable), implement the `await_suspend` overload
    on your coroutine return type:

    @code
    struct my_task
    {
        struct promise_type : io_awaitable_support<promise_type> { ... };

        std::coroutine_handle<promise_type> h_;

        // IoAwaitable await_suspend receives and stores the environment
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont, io_env const* env)
        {
            h_.promise().set_environment(env);
            // ... rest of suspend logic ...
        }
    };
    @endcode

    @par Thread Safety
    The environment is stored during `await_suspend` and read during
    `co_await this_coro::environment`. These occur on the same logical
    thread of execution, so no synchronization is required.

    @see this_coro::environment, this_coro::executor,
         this_coro::stop_token, this_coro::allocator
    @see io_env
    @see IoAwaitable
*/
template<typename Derived>
class io_awaitable_support
{
    io_env const* env_ = &detail::empty_io_env;
    mutable std::coroutine_handle<> cont_{std::noop_coroutine()};

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
        // Abnormal teardown: destroy orphaned continuation
        if(cont_ != std::noop_coroutine())
            cont_.destroy();
    }

    //----------------------------------------------------------
    // Continuation support
    //----------------------------------------------------------

    /** Store the continuation to resume on completion.

        Call this from your coroutine type's `await_suspend` overload
        to set up the completion path. The `final_suspend` awaiter
        returns this handle via unconditional symmetric transfer.

        @param cont The continuation to resume on completion.
    */
    void set_continuation(std::coroutine_handle<> cont) noexcept
    {
        cont_ = cont;
    }

    /** Return and consume the stored continuation handle.

        Resets the stored handle to `noop_coroutine()` so the
        destructor will not double-destroy it.

        @return The continuation for symmetric transfer.
    */
    std::coroutine_handle<> continuation() const noexcept
    {
        return std::exchange(cont_, std::noop_coroutine());
    }

    //----------------------------------------------------------
    // Environment support
    //----------------------------------------------------------

    /** Store a pointer to the execution environment.

        Call this from your coroutine type's `await_suspend`
        overload to make the environment available via
        `co_await this_coro::environment`. The pointed-to
        `io_env` must outlive this coroutine.

        @param env The environment to store.
    */
    void set_environment(io_env const* env) noexcept
    {
        env_ = env;
    }

    /** Return the stored execution environment.

        @return The environment.
    */
    io_env const* environment() const noexcept
    {
        return env_;
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

        This function handles @ref this_coro::environment_tag and
        the fine-grained tags (@ref this_coro::executor_tag,
        @ref this_coro::stop_token_tag, @ref this_coro::allocator_tag)
        specially, returning an awaiter that yields the stored value.
        All other awaitables are delegated to @ref transform_awaitable.

        @param t The awaited expression.

        @return An awaiter for the expression.
    */
    template<typename T>
    auto await_transform(T&& t)
    {
        using Tag = std::decay_t<T>;

        if constexpr (std::is_same_v<Tag, this_coro::environment_tag>)
        {
            struct awaiter
            {
                io_env const* env_;
                bool await_ready() const noexcept { return true; }
                void await_suspend(std::coroutine_handle<>) const noexcept { }
                io_env const* await_resume() const noexcept { return env_; }
            };
            return awaiter{env_};
        }
        else if constexpr (std::is_same_v<Tag, this_coro::executor_tag>)
        {
            struct awaiter
            {
                executor_ref executor_;
                bool await_ready() const noexcept { return true; }
                void await_suspend(std::coroutine_handle<>) const noexcept { }
                executor_ref await_resume() const noexcept { return executor_; }
            };
            return awaiter{env_->executor};
        }
        else if constexpr (std::is_same_v<Tag, this_coro::stop_token_tag>)
        {
            struct awaiter
            {
                std::stop_token token_;
                bool await_ready() const noexcept { return true; }
                void await_suspend(std::coroutine_handle<>) const noexcept { }
                std::stop_token await_resume() const noexcept { return token_; }
            };
            return awaiter{env_->stop_token};
        }
        else if constexpr (std::is_same_v<Tag, this_coro::allocator_tag>)
        {
            struct awaiter
            {
                std::pmr::memory_resource* allocator_;
                bool await_ready() const noexcept { return true; }
                void await_suspend(std::coroutine_handle<>) const noexcept { }
                std::pmr::memory_resource* await_resume() const noexcept { return allocator_; }
            };
            return awaiter{env_->allocator};
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
