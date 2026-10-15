//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_EX_ABANDON_HPP
#define BOOST_CAPY_EX_ABANDON_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <atomic>
#include <coroutine>

namespace boost {
namespace capy {

/** The owner of one segment of a coroutine chain.

    A segment is a run of coroutines started together: the task
    `run_async` launches and everything it awaits, a task `run` starts
    on another executor, or one branch of `when_all` or `when_any`. Its
    owner is the coroutine that started it, suspended above the
    segment's top frame until that frame completes. Every coroutine in
    the segment reaches the owner through its @ref io_env.

    When the holder of a suspended coroutine in the segment gives it up
    with @ref abandon, the owner posts its own continuation to its
    executor. There it unwinds the segment from the top: it destroys
    the frame it awaits, which destroys the frames below it in turn, and
    finishes without delivering a result. An executor that destroys the
    posted continuation instead, as a closed one does, unwinds the
    segment the same way.

    @par Thread Safety
    @ref abandon may be called from any thread. Only one holder can
    abandon a segment, since it is suspended at one point; a repeated
    call before the unwind runs does nothing.
*/
class chain_owner
{
    std::atomic<bool> abandoned_{false};
    continuation self_;
    executor_ref ex_;

public:
    /** Bind the owner to its coroutine and executor.

        Called by the owner before it starts the segment.

        @param self The owner's coroutine, resumed or destroyed to
            unwind the segment.

        @param ex The executor the segment runs on.
    */
    void bind(std::coroutine_handle<> self, executor_ref ex) noexcept
    {
        self_.h = self;
        ex_ = ex;
    }

    /// Return true once the segment has been abandoned.
    bool abandoned() const noexcept
    {
        return abandoned_.load(std::memory_order_acquire);
    }

    /** Unwind the segment on the owner's executor.

        Does nothing if the segment was already abandoned. Otherwise
        posts the owner's continuation; nothing of the owner is touched
        afterward, since the post may unwind and free it at once.

        @note A post that throws calls `std::terminate`.
    */
    void abandon() noexcept
    {
        if(abandoned_.exchange(true, std::memory_order_acq_rel))
            return;
        ex_.post(self_);
    }
};

/** Give up a suspended coroutine for good.

    Called by the holder of a suspended coroutine that will never resume
    it, such as an execution context, strand or primitive being torn
    down. The coroutine's segment is unwound from its owner: on the
    owner's executor, from the top frame down to this coroutine, without
    any frame delivering a result. Ordinary cancellation does not use
    this; it resumes the coroutine with an error instead.

    A continuation with no owner, such as an owner's own continuation,
    is destroyed directly.

    @par Preconditions
    `c` refers to a suspended coroutine that the caller holds and that
    nothing else will resume or abandon. If `c` has no owner, nothing
    awaits the coroutine: destroying its frame alone would leave any
    awaiting frame holding a destroyed one.

    @par Postconditions
    The caller must not touch the coroutine, or anything in its frame,
    again. The frames may already be destroyed when this returns, if the
    owner's executor destroys posted work at once.

    @note The unwind runs on the owner's executor, which may belong to
    another execution context than the caller's, and may run after the
    caller's context is destroyed. A chain that holds objects of the
    caller's context must therefore have finished before that context
    is torn down.

    @note Each frame is destroyed from the destructor of the frame
    awaiting it, so the unwind uses stack in proportion to how deeply
    the chain's coroutines are nested.

    @param c The continuation of the coroutine given up.
*/
inline void
abandon(continuation& c) noexcept
{
    if(c.owner)
        c.owner->abandon();
    else if(c.h)
        c.h.destroy();
}

} // namespace capy
} // namespace boost

#endif
