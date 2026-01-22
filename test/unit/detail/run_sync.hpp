//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_RUN_SYNC_HPP
#define BOOST_CAPY_TEST_RUN_SYNC_HPP

#include <boost/capy/coro.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/task.hpp>

#include <coroutine>
#include <exception>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

// Trivial execution context for synchronous execution.
class sync_context : public execution_context
{
};

// Trivial executor for synchronous execution.
struct sync_executor
{
    static sync_context ctx_;

    bool operator==(sync_executor const&) const noexcept { return true; }
    execution_context& context() const noexcept { return ctx_; }
    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    coro dispatch(coro h) const
    {
        return h;
    }

    void post(coro h) const
    {
        h.resume();
    }
};

inline sync_context sync_executor::ctx_;

// Synchronous task runner for tests.
class sync_runner
{
public:
    sync_runner() = default;

    sync_runner(sync_runner const&) = delete;
    sync_runner& operator=(sync_runner const&) = delete;
    sync_runner(sync_runner&&) = default;
    sync_runner& operator=(sync_runner&&) = default;

    template<typename T>
    T operator()(task<T> t) &&
    {
        auto h = t.release();
        sync_executor ex;

        h.promise().set_continuation(std::noop_coroutine(), ex);
        h.promise().set_executor(ex);

        ex.dispatch(coro{h}).resume();

        std::exception_ptr ep = h.promise().ep_;

        if constexpr (std::is_void_v<T>)
        {
            h.destroy();
            if (ep)
                std::rethrow_exception(ep);
        }
        else
        {
            if (ep)
            {
                h.destroy();
                std::rethrow_exception(ep);
            }
            auto& result_base = static_cast<task_return_base<T>&>(
                h.promise());
            auto result = std::move(*result_base.result_);
            h.destroy();
            return result;
        }
    }
};

inline
sync_runner
run_sync()
{
    return sync_runner{};
}

} // namespace detail
} // namespace capy
} // namespace boost

#endif
