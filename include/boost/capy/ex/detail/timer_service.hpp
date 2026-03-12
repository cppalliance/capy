//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_TIMER_SERVICE_HPP
#define BOOST_CAPY_EX_TIMER_SERVICE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/execution_context.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <unordered_set>
#include <vector>

namespace boost {
namespace capy {
namespace detail {

/* Shared timer thread for an execution_context.

   One background std::thread per execution_context. All timeouts
   scheduled through this context share the same thread, which sleeps
   on a condition variable until the next deadline.

   The timer thread never touches coroutine frames or executors
   directly — callbacks are responsible for posting work through
   the appropriate executor.
*/

class BOOST_CAPY_DECL
    timer_service
    : public execution_context::service
{
public:
    using timer_id = std::uint64_t;

    explicit timer_service(execution_context& ctx);

    // Calls shutdown() to join the background thread.
    // Handles the discard path in use_service_impl where
    // a duplicate service is deleted without shutdown().
    ~timer_service();

    /** Schedule a callback to fire after a duration.

        The callback is invoked on the timer service's background
        thread. It must not block for extended periods.

        @return An id that can be passed to cancel().
    */
    template<typename Rep, typename Period>
    timer_id schedule_after(
        std::chrono::duration<Rep, Period> dur,
        std::function<void()> cb)
    {
        auto deadline = std::chrono::steady_clock::now() + dur;
        return schedule_at(deadline, std::move(cb));
    }

    /** Cancel a pending timer.

        After this function returns, the callback is guaranteed
        not to be running and will never be invoked. If the
        callback is currently executing on the timer thread,
        this call blocks until it completes.

        Safe to call with any id, including ids that have
        already fired, been cancelled, or were never issued.
    */
    void cancel(timer_id id);

protected:
    void shutdown() override;

private:
    void stop_and_join();
    struct entry
    {
        std::chrono::steady_clock::time_point deadline;
        timer_id id;
        std::function<void()> callback;

        bool operator>(entry const& o) const noexcept
        {
            return deadline > o.deadline;
        }
    };

    timer_id schedule_at(
        std::chrono::steady_clock::time_point deadline,
        std::function<void()> cb);

    void run();

// warning C4251: std types need to have dll-interface
#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable: 4251)
#endif
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable cancel_cv_;
    std::priority_queue<
        entry,
        std::vector<entry>,
        std::greater<>> queue_;
    std::unordered_set<timer_id> active_ids_;
    timer_id next_id_ = 0;
    timer_id executing_id_ = 0;
    bool stopped_ = false;
    std::thread thread_;
#ifdef _MSC_VER
# pragma warning(pop)
#endif
};

} // detail
} // capy
} // boost

#endif
