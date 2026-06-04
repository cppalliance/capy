//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_TASK_HPP
#define BOOST_CAPY_IO_TASK_HPP

#include <boost/capy/io_result.hpp>
#include <boost/capy/task.hpp>
#include <coroutine>

namespace boost {
namespace capy {

/** A task type for I/O operations yielding io_result.

    This is a convenience alias for `task<io_result<Ts...>>`.
    The converting constructor on `io_result<>` allows direct
    `co_return` of error codes:

    @code
    io_task<> connect_to_server(socket& s, endpoint ep)
    {
        co_return co_await s.connect(ep);  // returns io_result<>
    }

    io_task<> handler(route_params& rp)
    {
        co_return route::next;  // error_code converts to io_result<>
    }
    @endcode

    @tparam Ts Additional value types beyond error_code.
*/
template<class... Ts>
struct io_task
{
    struct promise_type : task<io_result<Ts...>>::promise_type
    {
        io_task get_return_object()
        {
            return io_task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        friend io_task;

        // An io_task can yield an io_result value. That means the coroutie suspend if the error is set.
        template<typename ... Us>
            requires (std::constructible_from<Ts> && ...)
        auto yield_value(io_result<Us...> res)
        {
            struct awaiter
            {
                io_result<Us...>  res;
                bool await_ready() {return !res.ec;}

                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h)
                {
                    auto &p = h.promise();
                    p.return_value({res.ec, Ts()...});
                    
                    return p.continuation();
                }

                std::tuple<Us...> await_resume() 
                {
                    return std::move(res.values);
                }
            };

            return awaiter{std::move(res)};
        }
        

    };

    /// Destroy the task and its coroutine frame if owned.

    ~io_task()
    {
        if (h_)
             h_.destroy();
    }
    /// Return false; tasks are never immediately ready.
    bool await_ready() const noexcept
    {
        return false;
    }

    /// Return the result or rethrow any stored exception.
    auto await_resume()
    {
        if(h_.promise().has_ep_)
            std::rethrow_exception(h_.promise().ep_);
        return std::move(*h_.promise().result_);
    }



    /// Start execution with the caller's context.
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont, io_env const* env)
    {
        h_.promise().set_continuation(cont);
        h_.promise().set_environment(env);
        return h_;
    }

    /** Return the coroutine handle.

        @note Do not call `destroy()` on the returned handle while the
        task is being awaited. The task's lifetime is normally managed
        by `run_async`, `run`, or the awaiting parent; manually
        destroying a suspended task that another coroutine is awaiting
        produces undefined behavior. For cooperative cancellation, use
        `std::stop_token`.

        @return The coroutine handle.
    */
    std::coroutine_handle<promise_type> handle() const noexcept
    {
        return h_;
    }

    /** Release ownership of the coroutine frame.

        After calling this, destroying the task does not destroy the
        coroutine frame. The caller becomes responsible for the frame's
        lifetime.

        @note If the caller intends to call `destroy()` on the
        released handle, it must do so only when the task has not
        started or has fully completed. Destroying a suspended task
        that is being awaited produces undefined behavior.

        @par Postconditions
        `handle()` returns the original handle, but the task no longer
        owns it.
    */
    void release() noexcept
    {
        h_ = nullptr;
    }

    io_task(io_task const&) = delete;
    io_task& operator=(io_task const&) = delete;

    /// Construct by moving, transferring ownership.
    io_task(io_task&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
    {
    }

    /// Assign by moving, transferring ownership.
    io_task& operator=(io_task&& other) noexcept
    {
        if(this != &other)
        {
            if(h_)
                h_.destroy();
            h_ = std::exchange(other.h_, nullptr);
        }
        return *this;
    }


  private:
    explicit io_task(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }
  
    std::coroutine_handle<promise_type> h_;

  
};

} // namespace capy
} // namespace boost

#endif
