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
#include <boost/capy/ex/detail/recycling_frame_allocator.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/task.hpp>

#include <coroutine>
#include <exception>
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

/** Non-coroutine task runner with stack-based frame allocator.

    This class provides efficient coroutine frame allocation without
    requiring heap allocation for the allocator infrastructure itself.
    The frame allocator wrapper lives on the caller's stack, guaranteeing
    it outlives all coroutine frames that reference it.

    @tparam Dispatcher The dispatcher type for scheduling coroutine resumption.
    @tparam Allocator The frame allocator type (default: recycling_frame_allocator).
*/
template<
    dispatcher Dispatcher,
    frame_allocator Allocator>
class async_runner
{
    Dispatcher d_;
    frame_allocator_wrapper<Allocator> wrapper_;
    frame_allocator_base* saved_allocator_;

public:
    /** Construct an async_runner with dispatcher and allocator.

        Sets up thread-local storage to use the wrapper for subsequent
        coroutine frame allocations. Saves any existing TLS value for
        restoration on destruction (supports nested async_run calls).

        @param d The dispatcher for scheduling coroutine resumption.
        @param a The allocator for coroutine frame allocation.
    */
    explicit async_runner(Dispatcher d, Allocator a = {})
        : d_(std::move(d))
        , wrapper_(std::move(a))
        , saved_allocator_(frame_allocating_base::get_frame_allocator())
    {
        frame_allocating_base::set_frame_allocator(wrapper_);
    }

    /** Destructor restores previous thread-local storage value.
    */
    ~async_runner()
    {
        if(saved_allocator_)
            frame_allocating_base::set_frame_allocator(*saved_allocator_);
        else
            frame_allocating_base::clear_frame_allocator();
    }

    // Non-copyable, non-movable (wrapper address must remain stable)
    async_runner(async_runner const&) = delete;
    async_runner& operator=(async_runner const&) = delete;
    async_runner(async_runner&&) = delete;
    async_runner& operator=(async_runner&&) = delete;

    /** Run a task in fire-and-forget mode.

        Executes the task synchronously. Results are discarded,
        exceptions are rethrown.

        @param t The task to execute.
    */
    template<typename T>
    void operator()(task<T> t) &&
    {
        run_with_handler(std::move(t), default_handler{});
    }

    /** Run a task with a completion handler.

        Executes the task synchronously, then invokes the handler
        with the result or exception.

        @param t The task to execute.
        @param h The handler to invoke on completion.
    */
    template<typename T, typename Handler>
    void operator()(task<T> t, Handler h) &&
    {
        if constexpr (std::is_invocable_v<Handler, std::exception_ptr>)
        {
            run_with_handler(std::move(t), std::move(h));
        }
        else
        {
            using combined = handler_pair<Handler, default_handler>;
            run_with_handler(
                std::move(t),
                combined{std::move(h), default_handler{}});
        }
    }

    /** Run a task with separate success and error handlers.

        Executes the task synchronously, then invokes the appropriate
        handler based on success or failure.

        @param t The task to execute.
        @param on_success Handler for successful completion.
        @param on_error Handler for exceptions.
    */
    template<typename T, typename H1, typename H2>
    void operator()(task<T> t, H1 on_success, H2 on_error) &&
    {
        using combined = handler_pair<H1, H2>;
        run_with_handler(
            std::move(t),
            combined{std::move(on_success), std::move(on_error)});
    }

private:
    template<typename T, typename Handler>
    void run_with_handler(task<T> t, Handler h)
    {
        auto inner_handle = t.release();

        // Set up the task for execution
        inner_handle.promise().continuation_ = std::noop_coroutine();
        inner_handle.promise().ex_ = d_;
        inner_handle.promise().caller_ex_ = d_;
        inner_handle.promise().needs_dispatch_ = false;

        // Run synchronously
        d_(any_coro{inner_handle}).resume();

        // Extract result and invoke handler
        std::exception_ptr ep = inner_handle.promise().ep_;

        if constexpr (std::is_void_v<T>)
        {
            // Clean up before invoking handler
            inner_handle.destroy();

            if(ep)
                h(ep);
            else
                h();
        }
        else
        {
            if(ep)
            {
                inner_handle.destroy();
                h(ep);
            }
            else
            {
                auto& result_base = static_cast<task_return_base<T>&>(
                    inner_handle.promise());
                auto result = std::move(*result_base.result_);
                inner_handle.destroy();
                h(std::move(result));
            }
        }
    }
};

} // namespace detail

/** Creates a task runner with stack-based frame allocator.

    Returns an async_runner that manages coroutine frame allocation
    without heap-allocating the allocator infrastructure. The frame
    allocator wrapper lives on the caller's stack.

    @par Usage
    @code
    // Fire and forget - discards result, rethrows exceptions
    async_run(dispatcher)(my_coroutine());

    // With handler - captures result
    async_run(dispatcher)(compute_value(), [](int result) {
        std::cout << "Got: " << result << "\n";
    });

    // With separate success/error handlers
    async_run(dispatcher)(compute_value(),
        [](int result) { std::cout << "Got: " << result << "\n"; },
        [](std::exception_ptr) { std::cout << "Error!\n"; });
    @endcode

    @param d The dispatcher that schedules and resumes the task.
    @param alloc The frame allocator (default: recycling_frame_allocator).

    @return An async_runner with operator() to launch tasks.

    @see async_runner
    @see task
    @see dispatcher
*/
template<
    dispatcher Dispatcher,
    frame_allocator Allocator = detail::recycling_frame_allocator>
detail::async_runner<Dispatcher, Allocator>
async_run(Dispatcher d, Allocator alloc = {})
{
    return detail::async_runner<Dispatcher, Allocator>(
        std::move(d), std::move(alloc));
}

} // namespace capy
} // namespace boost

#endif
