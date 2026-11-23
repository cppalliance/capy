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
#include <boost/capy/concept/context_awaitable.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/executor_ref.hpp>

#include <atomic>
#include <coroutine>
#include <optional>
#include <type_traits>

namespace boost {
namespace capy {

/** The owner of one segment of a coroutine chain.

    A segment is a run of coroutines started together: the task
    `run_async` launches and everything it awaits, a task `run` starts
    on another executor, or one branch of `when_all` or `when_any`. Its
    owner is the coroutine that started it, suspended above the
    segment's top frame until that frame completes. Every coroutine in
    the segment reaches the owner through its @ref await_context.

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

/** Await an awaitable on behalf of a segment owner.

    An owner, a coroutine that binds a @ref chain_owner and starts a
    segment beneath it, waits for that segment by awaiting this. It is
    resumed either when the awaitable completes or to unwind the
    segment after an @ref abandon. In the second case the awaitable
    never completed, so there is no result to read: the result is
    empty, or `false` for an awaitable with no result, and the owner
    must destroy the awaitable without reading it.

    The awaitable is referenced, not copied, so the caller keeps it
    alive until the `co_await` completes. Its `await_resume` must
    return a move-constructible object or `void`: the result is moved
    into a `std::optional`, which cannot hold a reference.

    @par Preconditions
    The awaiting coroutine's @ref await_context names, as its `owner`,
    the @ref chain_owner bound to that coroutine.

    @par Exception Safety
    An exception from the awaitable's `await_ready`, `await_suspend` or
    `await_resume` propagates unchanged.

    @note Awaited from a promise built on
    @ref context_awaitable_promise_base, as an owner usually is, an
    exception from `await_ready` or `await_suspend` calls
    `std::terminate` instead.

    @par Example
    @par !example example

    @see chain_owner, abandon
*/
template<ContextAwaitable A>
class abandonable
{
    A& a_;
    chain_owner* owner_ = nullptr;

public:
    /// The awaitable's own `await_resume` result.
    using result_type = decltype(std::declval<A&>().await_resume());

    /** Construct from the awaitable to wait for.

        @param a The awaitable. It is referenced, not copied.
    */
    explicit abandonable(A& a) noexcept
        : a_(a)
    {
    }

    /// Check whether the awaitable is already complete.
    bool await_ready()
    {
        return a_.await_ready();
    }

    /** Suspend on the awaitable, remembering the segment owner.

        @param h The coroutine performing the `co_await`.
        @param env The environment of that coroutine.

        @return The awaitable's own `await_suspend` result.
    */
    template<class H>
    auto await_suspend(H h, await_context const* env)
    {
        owner_ = env->owner;
        return a_.await_suspend(h, env);
    }

    /** Return the result, or nothing if the segment was abandoned.

        For an awaitable with no result this returns `bool` instead,
        `false` when the segment was unwound.

        @return The result, empty when the segment was unwound.
    */
    auto await_resume()
    {
        bool const unwinding = owner_ && owner_->abandoned();
        if constexpr(std::is_void_v<result_type>)
        {
            if(unwinding)
                return false;
            a_.await_resume();
            return true;
        }
        else
        {
            if(unwinding)
                return std::optional<result_type>();
            return std::optional<result_type>(a_.await_resume());
        }
    }
};

} // namespace capy
} // namespace boost

#endif
