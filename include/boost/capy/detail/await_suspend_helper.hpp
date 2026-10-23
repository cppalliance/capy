//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_AWAIT_SUSPEND_HELPER_HPP
#define BOOST_CAPY_DETAIL_AWAIT_SUSPEND_HELPER_HPP

#include <coroutine>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/detail/config.hpp>

#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

/** Perform symmetric transfer, working around MSVC codegen bugs.

    MSVC stores the `std::coroutine_handle<>` returned from
    `await_suspend` in a hidden `__$ReturnUdt$` variable located
    on the coroutine frame. When another thread resumes or destroys
    the frame between the store and the read-back for the
    symmetric-transfer tail-call, the read hits freed memory.

    This occurs in two scenarios:

    @li `await_suspend` calls `h.destroy()` then returns a handle
        (e.g. `when_all_runner` and `when_any_runner` final_suspend).
        The return value is written to the now-destroyed frame.

    @li `await_suspend` hands the continuation to another thread
        via an executor handoff (e.g. `post()` or `dispatch()`),
        which may resume the parent. The parent can destroy this
        frame before the runtime reads `__$ReturnUdt$` (e.g.
        `boundary_trampoline` final_suspend).

    On affected compilers this function calls `h.resume()` on the
    current stack and returns `void`, causing unconditional
    suspension. The trade-off is O(n) stack growth instead of
    O(1) tail-calls.

    This defect affects MSVC 19.34 through 19.44 on every target
    and is fixed in MSVC 19.50 (VS 2026 / 18.0). Measured on
    19.44 the caller builds the hidden return slot at
    `__coro_frame_ptr$ + 0xC0`, on the coroutine frame; on 19.51
    it is an `rsp`-relative stack temporary, so destroying the
    frame no longer invalidates it.

    Do not widen this gate on the basis of Developer Community
    ticket 10251975, tagged "Fixed in VS 2022 17.9 Preview 2";
    19.39 reproduces the fault identically to 19.34.

    On ARM64 the workaround does not retire at 19.50, because
    that target has a second, unrelated defect. With a real
    symmetric transfer, MSVC ARM64 release loses the handler for
    a `try` region that spans the suspend point: a `throw`
    inside that region is not caught by the `catch` beside it and
    escapes to the promise's `unhandled_exception`. A `catch(...)`
    misses it too, so the region is not found at all rather than
    the handler failing to match. Any `await_suspend` that returns
    a handle triggers it, whether the coroutine resumes at once
    or later from another call stack, and whether the throw comes
    from `await_resume` or from the body after it. A standalone
    program without capy reproduces it on 19.44 and 19.51; the
    first clause of the gate already covers 19.44. Debug builds
    are unaffected, as is x64 at the same toolset.
    `testCatchAfterDeferredResume` in test/unit/task.cpp covers
    this.

    The void-returning transfer alone does not cover every case on
    19.51. See `await_ready_then_suspend` below for the second
    safeguard that `task` and `quitter` apply where this one is on.

    `_M_ARM64EC` is included conservatively. It generates ARM64
    code and has not been tested here; keeping the workaround on
    is the safe direction, since it costs stack depth rather than
    correctness.

    The gate deliberately excludes Clang by testing
    `BOOST_CAPY_MSVC` rather than `_MSC_VER`. Both `clang-cl` and
    `clang++` targeting Windows define `_MSC_VER` for ABI
    compatibility, but generate a correct tail-call.

    Note that a probe which merely poisons the destroyed frame
    cannot validate this gate. Routing the return through this
    function moves the frame write to after `destroy()`, which
    repairs the poison pattern and hides the defect. The
    regression test in
    test/unit/detail/await_suspend_helper.cpp unmaps the frame
    instead, so any post-destroy access faults.

    On unaffected compilers the handle is returned directly for
    proper symmetric transfer.

    Callers must use `auto` return type on their `await_suspend`
    so the return type adapts per platform.

    @param h The coroutine handle to transfer to.
*/
#if BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND
inline void symmetric_transfer(std::coroutine_handle<> h) noexcept
{
    // safe_resume is not needed here: the calling coroutine is
    // about to suspend unconditionally. When it later resumes,
    // await_resume restores TLS from the promise's environment.
    h.resume();
}
#else
inline std::coroutine_handle<>
symmetric_transfer(std::coroutine_handle<> h) noexcept
{
    return h;
}
#endif

#if BOOST_CAPY_SYMMETRIC_TRANSFER_WORKAROUND
/** Ask whether an awaitable is ready, and suspend on it if not.

    Used by the `transform_awaiter` of `task` and `quitter` where the
    symmetric-transfer workaround is on. There the awaiter's own
    `await_ready` returns `false`, and its `await_suspend` calls this
    instead, so the wrapped awaitable's `await_ready` runs inside
    `await_suspend` rather than at the start of the `co_await`.

    This avoids a second MSVC ARM64 release defect. When the wrapped
    `await_ready` is evaluated at the start of the `co_await`, and
    performs an atomic read-modify-write (as
    `async_waker::wait_awaiter` does to consume a latched wakeup), a
    `try` region spanning the `co_await` can lose its handler if a
    call precedes the `try`. This happens even with the workaround
    above in place. Observed on 19.51; not on 19.44. A plain or
    atomic load in `await_ready` was not affected. The regression
    tests are `testCatchAfterRmwAwaitReady` and
    `testCatchAroundWakerWait` in test/unit/task.cpp, and
    `testCatchAfterRmwAwaitReady` and
    `testCatchStopAfterRmwAwaitReady` in test/unit/quitter.cpp.

    The awaiting coroutine continues without suspending when the
    wrapped awaitable is ready, or when its `await_suspend` returns
    `false`. When the wrapped `await_suspend` returns a handle, that
    handle is resumed through `symmetric_transfer` and the awaiting
    coroutine stays suspended. Returning `false` instead of resuming
    inline keeps immediate completions from growing the stack.

    @param a The wrapped awaitable.

    @param h The coroutine performing the `co_await`.

    @param env The execution environment to pass to the wrapped
    awaitable's `await_suspend`.

    @return `false` to continue without suspending, otherwise `true`.
*/
template<class Awaitable>
bool
await_ready_then_suspend(
    Awaitable& a,
    std::coroutine_handle<> h,
    await_context const* env) noexcept
{
    // Called exactly once: await_ready may consume state.
    if(a.await_ready())
        return false;
    using R = decltype(a.await_suspend(h, env));
    if constexpr (std::is_same_v<R, bool>)
    {
        return a.await_suspend(h, env);
    }
    else if constexpr (std::is_void_v<R>)
    {
        a.await_suspend(h, env);
        return true;
    }
    else
    {
        symmetric_transfer(a.await_suspend(h, env));
        return true;
    }
}
#endif

// Helper to normalize await_suspend return types to std::coroutine_handle<>
template<typename Awaitable>
std::coroutine_handle<> call_await_suspend(
    Awaitable* a,
    std::coroutine_handle<> h,
    await_context const* env)
{
    using R = decltype(a->await_suspend(h, env));
    if constexpr (std::is_void_v<R>)
    {
        a->await_suspend(h, env);
        return std::noop_coroutine();
    }
    else if constexpr (std::is_same_v<R, bool>)
    {
        if(a->await_suspend(h, env))
            return std::noop_coroutine();
        return h;
    }
    else
    {
        return a->await_suspend(h, env);
    }
}

} // namespace detail
} // namespace capy
} // namespace boost

#endif
