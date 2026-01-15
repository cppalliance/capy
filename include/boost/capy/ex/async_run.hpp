//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASYNC_RUN_HPP
#define BOOST_CAPY_ASYNC_RUN_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/affine_awaitable.hpp>
#include <boost/capy/ex/detail/recycling_frame_allocator.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/task.hpp>

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace boost {
namespace capy {

namespace detail {

// Discards the result on success, rethrows on exception.
struct default_handler
{
    template<typename T>
    void operator()(T&&) const noexcept
    {
    }

    void operator()() const noexcept
    {
    }

    void operator()(std::exception_ptr ep) const
    {
        if(ep)
            std::rethrow_exception(ep);
    }
};

// Combines two handlers into one: h1 for success, h2 for exception.
template<typename H1, typename H2>
struct handler_pair
{
    H1 h1_;
    H2 h2_;

    template<typename T>
    void operator()(T&& v)
    {
        h1_(std::forward<T>(v));
    }

    void operator()()
    {
        h1_();
    }

    void operator()(std::exception_ptr ep)
    {
        h2_(ep);
    }
};

/** Suspended coroutine launcher using the suspended coroutine pattern.

    This coroutine is created by async_run() and suspends immediately after
    setting up the frame allocator. Its frame (Frame #2) is allocated BEFORE
    the user's task (Frame #1), ensuring proper lifetime ordering.

    The embedder lives on this coroutine's stack, guaranteeing it outlives
    the embedded wrapper in the user's task frame.
*/
template<
    dispatcher Dispatcher,
    frame_allocator Allocator>
struct async_run_launcher
{
    struct promise_type
    {
        std::optional<Dispatcher> d_;
        detail::embedding_frame_allocator<Allocator> embedder_;
        std::coroutine_handle<> inner_handle_;
        std::coroutine_handle<> continuation_;

        // Constructor that takes dispatcher and allocator from async_run parameters
        template<typename D, typename A>
        promise_type(D&& d, A&& a)
            : d_(std::forward<D>(d))
            , embedder_(std::forward<A>(a))
        {
            // Set TLS immediately so it's available for nested coroutine allocations
            frame_allocating_base::set_frame_allocator(embedder_);
        }

        // Default constructor (required but should not be used in normal flow)
        promise_type()
            : embedder_(Allocator{})
        {
        }

        async_run_launcher get_return_object()
        {
            return async_run_launcher{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        struct final_awaiter
        {
            bool await_ready() noexcept { return false; }

            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> h) noexcept
            {
                // Clear TLS after inner task completes
                frame_allocating_base::clear_frame_allocator();

                // Return continuation (or noop for fire-and-forget).
                // In fire-and-forget mode, we return noop and the launcher
                // will be destroyed by launch_awaitable's destructor after resume() returns.
                auto cont = h.promise().continuation_;
                return cont ? cont : std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept { return {}; }

        void unhandled_exception() { throw; }
        void return_void() {}

        // Awaitable to transfer control to inner task
        struct transfer_to_inner
        {
            promise_type* p_;

            bool await_ready() noexcept { return false; }

            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<>) noexcept
            {
                return p_->inner_handle_;
            }

            void await_resume() noexcept {}
        };
    };

    std::coroutine_handle<promise_type> handle_;

    // Awaitable to get promise without suspending
    struct get_promise
    {
        promise_type* p_;

        bool await_ready() noexcept { return false; }

        bool await_suspend(std::coroutine_handle<promise_type> h) noexcept
        {
            p_ = &h.promise();
            return false;  // Don't suspend
        }

        promise_type& await_resume() noexcept { return *p_; }
    };

    template<typename T>
    struct launch_awaitable
    {
        std::coroutine_handle<promise_type> launcher_;
        std::coroutine_handle<typename task<T>::promise_type> inner_;
        Dispatcher d_;
        bool started_ = false;

        launch_awaitable(
            std::coroutine_handle<promise_type> launcher,
            std::coroutine_handle<typename task<T>::promise_type> inner,
            Dispatcher d)
            : launcher_(launcher)
            , inner_(inner)
            , d_(std::move(d))
        {
        }

        ~launch_awaitable()
        {
            // If not awaited, run fire-and-forget style
            if(!started_ && launcher_)
            {
                // Store inner handle in launcher's promise
                launcher_.promise().inner_handle_ = inner_;

                // Fire-and-forget: no continuation
                launcher_.promise().continuation_ = std::noop_coroutine();
                inner_.promise().continuation_ = launcher_;
                inner_.promise().ex_ = d_;
                inner_.promise().caller_ex_ = d_;
                inner_.promise().needs_dispatch_ = false;

                // Run synchronously
                d_(any_coro{launcher_}).resume();

                // Clean up
                inner_.destroy();
                launcher_.destroy();
            }
        }

        // Move-only
        launch_awaitable(launch_awaitable&& o) noexcept
            : launcher_(std::exchange(o.launcher_, nullptr))
            , inner_(std::exchange(o.inner_, nullptr))
            , d_(std::move(o.d_))
            , started_(o.started_)
        {
        }

        launch_awaitable(launch_awaitable const&) = delete;
        launch_awaitable& operator=(launch_awaitable const&) = delete;
        launch_awaitable& operator=(launch_awaitable&&) = delete;

        bool await_ready() noexcept { return false; }

        // Affine awaitable interface: takes continuation and dispatcher
        template<typename D>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont, D const&)
        {
            started_ = true;

            // Store inner handle in launcher's promise
            launcher_.promise().inner_handle_ = inner_;

            // Set up continuation chain: cont <- launcher <- inner
            launcher_.promise().continuation_ = cont;
            inner_.promise().continuation_ = launcher_;
            inner_.promise().ex_ = d_;
            inner_.promise().caller_ex_ = d_;
            inner_.promise().needs_dispatch_ = false;  // Direct transfer, no dispatch

            // Transfer to launcher (which will transfer to inner)
            return launcher_;
        }

        T await_resume()
        {
            // Get result from inner task
            auto& inner_promise = inner_.promise();
            std::exception_ptr ep = inner_promise.ep_;

            // Clean up handles
            inner_.destroy();
            launcher_.destroy();
            launcher_ = nullptr;  // Prevent destructor from running

            if(ep)
                std::rethrow_exception(ep);

            if constexpr (!std::is_void_v<T>)
            {
                auto& result_base = static_cast<detail::task_return_base<T>&>(inner_promise);
                return std::move(*result_base.result_);
            }
        }
    };

    // operator() returning awaitable (can be co_awaited or run fire-and-forget)
    template<typename T>
    launch_awaitable<T> operator()(task<T> inner) &&
    {
        auto d = std::move(*handle_.promise().d_);
        auto launcher = handle_;
        handle_ = nullptr;  // Prevent destructor from destroying
        return launch_awaitable<T>{launcher, inner.release(), std::move(d)};
    }

    // operator() with handler - runs fire-and-forget and calls handler with result
    template<typename T, typename Handler>
    void operator()(task<T> inner, Handler h) &&
    {
        auto d = std::move(*handle_.promise().d_);

        if constexpr (std::is_invocable_v<Handler, std::exception_ptr>)
        {
            // Handler handles exceptions itself
            std::move(*this).run_with_handler(std::move(inner), std::move(h), std::move(d));
        }
        else
        {
            // Handler only handles success - pair with default exception handler
            using combined = handler_pair<Handler, default_handler>;
            std::move(*this).run_with_handler(
                std::move(inner),
                combined{std::move(h), default_handler{}},
                std::move(d));
        }
    }

    // operator() with separate success/error handlers
    template<typename T, typename H1, typename H2>
    void operator()(task<T> inner, H1 h1, H2 h2) &&
    {
        auto d = std::move(*handle_.promise().d_);

        using combined = handler_pair<H1, H2>;
        std::move(*this).run_with_handler(
            std::move(inner),
            combined{std::move(h1), std::move(h2)},
            std::move(d));
    }

    ~async_run_launcher()
    {
        if(handle_)
            handle_.destroy();
    }

    // Move-only
    async_run_launcher(async_run_launcher&& o) noexcept
        : handle_(std::exchange(o.handle_, nullptr))
    {}

    async_run_launcher(async_run_launcher const&) = delete;
    async_run_launcher& operator=(async_run_launcher const&) = delete;
    async_run_launcher& operator=(async_run_launcher&&) = delete;

private:
    explicit async_run_launcher(std::coroutine_handle<promise_type> h)
        : handle_(h)
    {}

    template<dispatcher D, frame_allocator A>
    friend async_run_launcher<D, A> async_run(D, A);

    // Run with handler - executes synchronously then invokes handler
    template<typename T, typename Handler>
    void run_with_handler(task<T> inner, Handler h, Dispatcher d)
    {
        auto inner_handle = inner.release();

        // Store inner handle in launcher's promise
        handle_.promise().inner_handle_ = inner_handle;

        // Fire-and-forget: no continuation
        handle_.promise().continuation_ = std::noop_coroutine();
        inner_handle.promise().continuation_ = handle_;
        inner_handle.promise().ex_ = d;
        inner_handle.promise().caller_ex_ = d;
        inner_handle.promise().needs_dispatch_ = false;

        // Run synchronously
        auto launcher = handle_;
        handle_ = nullptr;  // Prevent destructor from destroying
        d(any_coro{launcher}).resume();

        // Get result from inner task and invoke handler
        std::exception_ptr ep = inner_handle.promise().ep_;

        if constexpr (std::is_void_v<T>)
        {
            if(ep)
                h(ep);
            else
                h();
        }
        else
        {
            if(ep)
                h(ep);
            else
            {
                auto& result_base = static_cast<detail::task_return_base<T>&>(
                    inner_handle.promise());
                h(std::move(*result_base.result_));
            }
        }

        // Clean up
        inner_handle.destroy();
        launcher.destroy();
    }
};

} // namespace detail

/** Creates a launcher coroutine to launch lazy tasks for detached execution.

    Returns a suspended coroutine launcher whose frame is allocated BEFORE
    the user's task. This ensures the embedder (which lives on the launcher's
    stack frame) outlives the embedded wrapper in the user's task frame,
    preventing use-after-free bugs.

    This implementation uses the "suspended coroutine launcher" pattern to
    achieve exactly two coroutine frames with guaranteed allocation order.

    @par Usage
    @code
    io_context ioc;
    auto ex = ioc.get_executor();

    // Fire and forget - discards result, rethrows exceptions
    async_run(ex)(my_coroutine());

    // With handler - captures result
    async_run(ex)(compute_value(), [](int result) {
        std::cout << "Got: " << result << "\n";
    });

    // Awaitable mode - co_await to get result
    task<void> caller(auto ex) {
        int result = co_await async_run(ex)(compute_value());
        std::cout << "Got: " << result << "\n";
    }

    ioc.run();
    @endcode

    @param d The dispatcher that schedules and resumes the task.
    @param alloc The frame allocator (default: recycling_frame_allocator).

    @return A suspended async_run_launcher with operator() to launch tasks.

    @see async_run_launcher
    @see task
    @see dispatcher
*/
template<
    dispatcher Dispatcher,
    frame_allocator Allocator = detail::recycling_frame_allocator>
detail::async_run_launcher<Dispatcher, Allocator>
async_run(Dispatcher, Allocator = {})
{
    // Get promise without suspending - TLS was already set in promise constructor
    auto& promise = co_await typename detail::async_run_launcher<
        Dispatcher, Allocator>::get_promise{};

    // Transfer control to inner task (user's task)
    co_await typename detail::async_run_launcher<
        Dispatcher, Allocator>::promise_type::transfer_to_inner{&promise};

    // When we resume here, inner task has completed.
    // TLS is cleared in final_suspend before returning to continuation.
}

} // namespace capy
} // namespace boost

#endif
