//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_RUN_HPP
#define BOOST_CAPY_RUN_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/detail/run.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/context_runnable.hpp>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <coroutine>
#include <boost/capy/ex/frame_alloc_mixin.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/await_context.hpp>

#include <exception>
#include <memory_resource>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <variant>

/*
    Allocator Lifetime Strategy
    ===========================

    When using run() with a custom allocator:

        co_await run(ex, alloc)(my_task());

    The wrapper run(ex, alloc) returns is a temporary holding the
    frame_memory_resource my_task()'s frame is allocated from, so it must
    live until that frame is freed: the awaitable is awaited in the
    full-expression that made the wrapper, and the temporary lives until
    the co_await completes. Keeping the awaitable and awaiting it later
    frees the task's frame through a dead resource.

    The awaitable also holds a copy of the allocator. In await_suspend it
    overwrites the promise's saved frame_allocator pointer with the
    awaitable's resource, so tasks the inner task creates allocate from
    it through TLS.
*/

namespace boost::capy::detail {

/** Minimal coroutine that dispatches through the caller's executor.

    Sits between the inner task and the parent when executors
    diverge. The inner task's `final_suspend` resumes this
    trampoline via symmetric transfer. The trampoline's own
    `final_suspend` posts the parent through the caller's executor to
    restore the correct execution context.

    It also owns the inner task's segment. Abandoning that segment
    resumes the trampoline on the inner executor, or has a closed
    executor destroy it: either way it destroys the inner task, which
    unwinds the segment, then abandons the caller's segment, which
    unwinds the parent on its own executor. The caller's execution
    context must outlive that.
*/
struct boundary_trampoline
{
    struct promise_type
        : detail::process_frame_mixin
    {
        executor_ref caller_ex_;
        continuation parent_;
        chain_owner segment_;
        // The awaitable's trampoline object until the inner task
        // completes or is unwound; null afterward.
        boundary_trampoline* owner_ = nullptr;
        // Destroys the inner task the awaitable owns.
        void (*destroy_inner_)(void*) noexcept = nullptr;
        void* awaitable_ = nullptr;

        // Unwind the inner segment, then hand the parent to its own
        // segment's owner. The last touch of this frame: abandoning the
        // parent can destroy the awaitable, and this frame with it.
        void unwind() noexcept
        {
            destroy_inner_(awaitable_);
            owner_ = nullptr;
            auto parent = parent_;
            capy::abandon(parent);
        }

        ~promise_type()
        {
            // Linked only when a closed executor destroyed this frame
            // instead of resuming it to unwind.
            if(owner_)
            {
                owner_->h_ = nullptr;
                unwind();
            }
        }

        boundary_trampoline get_return_object() noexcept
        {
            return boundary_trampoline{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() const noexcept { return false; }

                auto await_suspend(
                    std::coroutine_handle<>) noexcept
                {
                    // Nothing here touches p_ after the post or the
                    // unwind: either can destroy this frame.
                    if(p_->segment_.abandoned())
                    {
                        p_->unwind();
                    }
                    else
                    {
                        p_->owner_ = nullptr;
                        auto ex = p_->caller_ex_;
                        ex.post(p_->parent_);
                    }
                    return detail::symmetric_transfer(
                        std::noop_coroutine());
                }

                void await_resume() const noexcept {} // LCOV_EXCL_LINE final_suspend awaiter, never resumed
            };
            return awaiter{this};
        }

        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };

    std::coroutine_handle<promise_type> h_{nullptr};

    boundary_trampoline() noexcept = default;

    ~boundary_trampoline()
    {
        if(h_)
        {
            h_.promise().owner_ = nullptr;
            h_.destroy();
        }
    }

    boundary_trampoline(boundary_trampoline const&) = delete;
    boundary_trampoline& operator=(boundary_trampoline const&) = delete;

    boundary_trampoline(boundary_trampoline&& o) noexcept
        : h_(std::exchange(o.h_, nullptr)) {}

    boundary_trampoline& operator=(boundary_trampoline&& o) noexcept
    {
        if(this != &o)
        {
            if(h_)
            {
                h_.promise().owner_ = nullptr;
                h_.destroy();
            }
            h_ = std::exchange(o.h_, nullptr);
        }
        return *this;
    }

private:
    explicit boundary_trampoline(std::coroutine_handle<promise_type> h) noexcept
        : h_(h) {}
};

inline boundary_trampoline make_boundary_trampoline()
{
    co_return;
}

/** Awaitable that binds a ContextRunnable to a specific executor.

    Stores the executor and inner task by value. When co_awaited, the
    co_await expression's lifetime extension keeps both alive for the
    duration of the operation.

    A dispatch trampoline handles the executor switch on completion:
    the inner task's `final_suspend` resumes the trampoline, which
    dispatches back through the caller's executor.

    The `await_context` is owned by this awaitable and is guaranteed to
    outlive the inner task and all awaitables in its chain. Awaitables
    may store `await_context const*` without concern for dangling references.

    @tparam Task The ContextRunnable type
    @tparam Ex The executor type
    @tparam InheritStopToken If true, inherit caller's stop token
    @tparam Alloc The allocator type (void for no allocator)
*/
template<ContextRunnable Task, Executor Ex, bool InheritStopToken, class Alloc = void>
struct [[nodiscard]] run_awaitable_ex
{
    Ex ex_;
    frame_memory_resource<Alloc> resource_;
    std::conditional_t<InheritStopToken, std::monostate, std::stop_token> st_;
    await_context env_;
    executor_ref caller_ex_;
    boundary_trampoline tr_;
    continuation task_cont_;
    bool holds_work_ = false;
    Task inner_;  // Last: destroyed first, while env_ is still valid

    ~run_awaitable_ex()
    {
        // Only a parent destroyed from above leaves the trampoline
        // still linked: the inner task is still running.
        if(tr_.h_ && tr_.h_.promise().owner_)
            std::terminate(); // LCOV_EXCL_LINE reached only in a death test's child process
        // A parent unwound instead of resumed never reaches
        // await_resume.
        if(holds_work_)
            caller_ex_.on_work_finished();
    }

    // Called by the trampoline to unwind the inner segment.
    static void destroy_inner(void* p) noexcept
    {
        auto& self = *static_cast<run_awaitable_ex*>(p);
        auto h = self.inner_.handle();
        self.inner_.release();
        h.destroy();
    }

    // void allocator, inherit stop token
    run_awaitable_ex(Ex ex, Task inner)
        requires (InheritStopToken && std::is_void_v<Alloc>)
        : ex_(std::move(ex))
        , inner_(std::move(inner))
    {
    }

    // void allocator, explicit stop token
    run_awaitable_ex(Ex ex, Task inner, std::stop_token st)
        requires (!InheritStopToken && std::is_void_v<Alloc>)
        : ex_(std::move(ex))
        , st_(std::move(st))
        , inner_(std::move(inner))
    {
    }

    // with allocator, inherit stop token (use template to avoid void parameter)
    template<class A>
        requires (InheritStopToken && !std::is_void_v<Alloc> && std::same_as<A, Alloc>)
    run_awaitable_ex(Ex ex, A alloc, Task inner)
        : ex_(std::move(ex))
        , resource_(std::move(alloc))
        , inner_(std::move(inner))
    {
    }

    // with allocator, explicit stop token (use template to avoid void parameter)
    template<class A>
        requires (!InheritStopToken && !std::is_void_v<Alloc> && std::same_as<A, Alloc>)
    run_awaitable_ex(Ex ex, A alloc, Task inner, std::stop_token st)
        : ex_(std::move(ex))
        , resource_(std::move(alloc))
        , st_(std::move(st))
        , inner_(std::move(inner))
    {
    }

    bool await_ready() const noexcept
    {
        return inner_.await_ready();
    }

    decltype(auto) await_resume()
    {
        if(std::exchange(holds_work_, false))
            caller_ex_.on_work_finished();
        return inner_.await_resume();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont, await_context const* caller_env)
    {
        tr_ = make_boundary_trampoline();
        auto& tp = tr_.h_.promise();
        tp.owner_ = &tr_;
        tp.caller_ex_ = caller_env->executor;
        tp.parent_.h = cont;
        tp.parent_.owner = caller_env->owner;
        tp.destroy_inner_ = &run_awaitable_ex::destroy_inner;
        tp.awaitable_ = this;
        tp.segment_.bind(tr_.h_, ex_);
        caller_ex_ = caller_env->executor;
        // Released once the parent resumes or is unwound. Not on the
        // post back: the parent's chain may own the executor object and
        // can be gone by the time post returns.
        caller_ex_.on_work_started();
        holds_work_ = true;

        auto h = inner_.handle();
        auto& p = h.promise();
        p.set_continuation(tr_.h_);

        env_.owner = &tp.segment_;
        env_.executor = ex_;
        if constexpr (InheritStopToken)
            env_.stop_token = caller_env->stop_token;
        else
            env_.stop_token = st_;

        if constexpr (!std::is_void_v<Alloc>)
            env_.frame_allocator = resource_.get();
        else
            env_.frame_allocator = caller_env->frame_allocator;

        p.set_environment(&env_);
        task_cont_.h = h;
        task_cont_.owner = &tp.segment_;
        try
        {
            ex_.post(task_cont_);
        }
        catch(...)
        {
            // The parent resumes with the exception: nothing may still
            // treat the task as lent or hold work for the parent.
            p.set_continuation(std::noop_coroutine());
            task_cont_.h = nullptr;
            tr_ = boundary_trampoline();
            holds_work_ = false;
            caller_ex_.on_work_finished();
            throw;
        }
        return std::noop_coroutine();
    }

    // Non-copyable
    run_awaitable_ex(run_awaitable_ex const&) = delete;
    run_awaitable_ex& operator=(run_awaitable_ex const&) = delete;

    // Movable (no noexcept - Task may throw)
    run_awaitable_ex(run_awaitable_ex&&) = default;
    run_awaitable_ex& operator=(run_awaitable_ex&&) = default;
};

/** Awaitable that runs a task with optional stop_token override.

    Does NOT store an executor - the task inherits the caller's executor
    directly. Executors always match, so no dispatch trampoline is needed.
    The inner task's `final_suspend` resumes the parent directly via
    unconditional symmetric transfer.

    @tparam Task The ContextRunnable type
    @tparam InheritStopToken If true, inherit caller's stop token
    @tparam Alloc The allocator type (void for no allocator)
*/
template<ContextRunnable Task, bool InheritStopToken, class Alloc = void>
struct [[nodiscard]] run_awaitable
{
    frame_memory_resource<Alloc> resource_;
    std::conditional_t<InheritStopToken, std::monostate, std::stop_token> st_;
    await_context env_;
    // Set while the parent is suspended awaiting the inner task.
    bool lent_ = false;
    Task inner_;  // Last: destroyed first, while env_ is still valid

    ~run_awaitable()
    {
        // Only the unwind of an abandoned chain destroys an inner task
        // still awaited; checked here so it terminates whatever the
        // task type does when destroyed while lent.
        auto h = inner_.handle();
        if(lent_ && h && !h.done() &&
            !(env_.owner && env_.owner->abandoned()))
            std::terminate(); // LCOV_EXCL_LINE reached only in a death test's child process
    }

    // void allocator, inherit stop token
    explicit run_awaitable(Task inner)
        requires (InheritStopToken && std::is_void_v<Alloc>)
        : inner_(std::move(inner))
    {
    }

    // void allocator, explicit stop token
    run_awaitable(Task inner, std::stop_token st)
        requires (!InheritStopToken && std::is_void_v<Alloc>)
        : st_(std::move(st))
        , inner_(std::move(inner))
    {
    }

    // with allocator, inherit stop token (use template to avoid void parameter)
    template<class A>
        requires (InheritStopToken && !std::is_void_v<Alloc> && std::same_as<A, Alloc>)
    run_awaitable(A alloc, Task inner)
        : resource_(std::move(alloc))
        , inner_(std::move(inner))
    {
    }

    // with allocator, explicit stop token (use template to avoid void parameter)
    template<class A>
        requires (!InheritStopToken && !std::is_void_v<Alloc> && std::same_as<A, Alloc>)
    run_awaitable(A alloc, Task inner, std::stop_token st)
        : resource_(std::move(alloc))
        , st_(std::move(st))
        , inner_(std::move(inner))
    {
    }

    bool await_ready() const noexcept
    {
        return inner_.await_ready();
    }

    decltype(auto) await_resume()
    {
        lent_ = false;
        return inner_.await_resume();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont, await_context const* caller_env)
    {
        auto h = inner_.handle();
        auto& p = h.promise();
        p.set_continuation(cont);
        lent_ = true;

        env_.owner = caller_env->owner;
        env_.executor = caller_env->executor;
        if constexpr (InheritStopToken)
            env_.stop_token = caller_env->stop_token;
        else
            env_.stop_token = st_;

        if constexpr (!std::is_void_v<Alloc>)
            env_.frame_allocator = resource_.get();
        else
            env_.frame_allocator = caller_env->frame_allocator;

        p.set_environment(&env_);
        return h;
    }

    // Non-copyable
    run_awaitable(run_awaitable const&) = delete;
    run_awaitable& operator=(run_awaitable const&) = delete;

    // Movable (no noexcept - Task may throw)
    run_awaitable(run_awaitable&&) = default;
    run_awaitable& operator=(run_awaitable&&) = default;
};

/** Wrapper returned by run(ex, ...) that accepts a task for execution.

    @tparam Ex The executor type.
    @tparam InheritStopToken If true, inherit caller's stop token.
    @tparam Alloc The allocator type (void for no allocator).
*/
template<Executor Ex, bool InheritStopToken, class Alloc>
class [[nodiscard]] run_wrapper_ex
{
    Ex ex_;
    std::conditional_t<InheritStopToken, std::monostate, std::stop_token> st_;
    frame_memory_resource<Alloc> resource_;
    Alloc alloc_;  // Copy to pass to awaitable

    std::pmr::memory_resource* saved_tls_ =
        get_current_frame_allocator();
    bool restored_ = false;

public:
    run_wrapper_ex(Ex ex, Alloc alloc)
        requires InheritStopToken
        : ex_(std::move(ex))
        , resource_(alloc)
        , alloc_(std::move(alloc))
    {
        set_current_frame_allocator(&resource_);
    }

    run_wrapper_ex(Ex ex, std::stop_token st, Alloc alloc)
        requires (!InheritStopToken)
        : ex_(std::move(ex))
        , st_(std::move(st))
        , resource_(alloc)
        , alloc_(std::move(alloc))
    {
        set_current_frame_allocator(&resource_);
    }

    // Restores the caller's allocator when evaluating the task argument
    // threw. Not afterwards: a wrapper destroyed with a torn-down parent
    // frame must not touch the tearing thread's allocator.
    ~run_wrapper_ex()
    {
        if(!restored_)
            set_current_frame_allocator(saved_tls_);
    }

    // Non-copyable, non-movable (must be used immediately)
    run_wrapper_ex(run_wrapper_ex const&) = delete;
    run_wrapper_ex(run_wrapper_ex&&) = delete;
    run_wrapper_ex& operator=(run_wrapper_ex const&) = delete;
    run_wrapper_ex& operator=(run_wrapper_ex&&) = delete;

    template<ContextRunnable Task>
    [[nodiscard]] auto operator()(Task t) &&
    {
        // The task's frame is made; the caller's own frames are not.
        set_current_frame_allocator(saved_tls_);
        restored_ = true;
        if constexpr (InheritStopToken)
            return run_awaitable_ex<Task, Ex, true, Alloc>{
                std::move(ex_), std::move(alloc_), std::move(t)};
        else
            return run_awaitable_ex<Task, Ex, false, Alloc>{
                std::move(ex_), std::move(alloc_), std::move(t), std::move(st_)};
    }
};

/// Specialization for memory_resource* - stores pointer directly.
template<Executor Ex, bool InheritStopToken>
class [[nodiscard]] run_wrapper_ex<Ex, InheritStopToken, std::pmr::memory_resource*>
{
    Ex ex_;
    std::conditional_t<InheritStopToken, std::monostate, std::stop_token> st_;
    std::pmr::memory_resource* mr_;

    std::pmr::memory_resource* saved_tls_ =
        get_current_frame_allocator();
    bool restored_ = false;

public:
    run_wrapper_ex(Ex ex, std::pmr::memory_resource* mr)
        requires InheritStopToken
        : ex_(std::move(ex))
        , mr_(mr)
    {
        set_current_frame_allocator(mr_);
    }

    run_wrapper_ex(Ex ex, std::stop_token st, std::pmr::memory_resource* mr)
        requires (!InheritStopToken)
        : ex_(std::move(ex))
        , st_(std::move(st))
        , mr_(mr)
    {
        set_current_frame_allocator(mr_);
    }

    // Restores the caller's allocator when evaluating the task argument
    // threw. Not afterwards: a wrapper destroyed with a torn-down parent
    // frame must not touch the tearing thread's allocator.
    ~run_wrapper_ex()
    {
        if(!restored_)
            set_current_frame_allocator(saved_tls_);
    }

    // Non-copyable, non-movable (must be used immediately)
    run_wrapper_ex(run_wrapper_ex const&) = delete;
    run_wrapper_ex(run_wrapper_ex&&) = delete;
    run_wrapper_ex& operator=(run_wrapper_ex const&) = delete;
    run_wrapper_ex& operator=(run_wrapper_ex&&) = delete;

    template<ContextRunnable Task>
    [[nodiscard]] auto operator()(Task t) &&
    {
        // The task's frame is made; the caller's own frames are not.
        set_current_frame_allocator(saved_tls_);
        restored_ = true;
        if constexpr (InheritStopToken)
            return run_awaitable_ex<Task, Ex, true, std::pmr::memory_resource*>{
                std::move(ex_), mr_, std::move(t)};
        else
            return run_awaitable_ex<Task, Ex, false, std::pmr::memory_resource*>{
                std::move(ex_), mr_, std::move(t), std::move(st_)};
    }
};

/// Specialization for no allocator (void).
template<Executor Ex, bool InheritStopToken>
class [[nodiscard]] run_wrapper_ex<Ex, InheritStopToken, void>
{
    Ex ex_;
    std::conditional_t<InheritStopToken, std::monostate, std::stop_token> st_;

public:
    explicit run_wrapper_ex(Ex ex)
        requires InheritStopToken
        : ex_(std::move(ex))
    {
    }

    run_wrapper_ex(Ex ex, std::stop_token st)
        requires (!InheritStopToken)
        : ex_(std::move(ex))
        , st_(std::move(st))
    {
    }

    // Non-copyable, non-movable (must be used immediately)
    run_wrapper_ex(run_wrapper_ex const&) = delete;
    run_wrapper_ex(run_wrapper_ex&&) = delete;
    run_wrapper_ex& operator=(run_wrapper_ex const&) = delete;
    run_wrapper_ex& operator=(run_wrapper_ex&&) = delete;

    template<ContextRunnable Task>
    [[nodiscard]] auto operator()(Task t) &&
    {
        if constexpr (InheritStopToken)
            return run_awaitable_ex<Task, Ex, true>{
                std::move(ex_), std::move(t)};
        else
            return run_awaitable_ex<Task, Ex, false>{
                std::move(ex_), std::move(t), std::move(st_)};
    }
};

/** Wrapper returned by run(st) or run(alloc) that accepts a task.

    @tparam InheritStopToken If true, inherit caller's stop token.
    @tparam Alloc The allocator type (void for no allocator).
*/
template<bool InheritStopToken, class Alloc>
class [[nodiscard]] run_wrapper
{
    std::conditional_t<InheritStopToken, std::monostate, std::stop_token> st_;
    frame_memory_resource<Alloc> resource_;
    Alloc alloc_;  // Copy to pass to awaitable

    std::pmr::memory_resource* saved_tls_ =
        get_current_frame_allocator();
    bool restored_ = false;

public:
    explicit run_wrapper(Alloc alloc)
        requires InheritStopToken
        : resource_(alloc)
        , alloc_(std::move(alloc))
    {
        set_current_frame_allocator(&resource_);
    }

    run_wrapper(std::stop_token st, Alloc alloc)
        requires (!InheritStopToken)
        : st_(std::move(st))
        , resource_(alloc)
        , alloc_(std::move(alloc))
    {
        set_current_frame_allocator(&resource_);
    }

    // Restores the caller's allocator when evaluating the task argument
    // threw. Not afterwards: a wrapper destroyed with a torn-down parent
    // frame must not touch the tearing thread's allocator.
    ~run_wrapper()
    {
        if(!restored_)
            set_current_frame_allocator(saved_tls_);
    }

    // Non-copyable, non-movable (must be used immediately)
    run_wrapper(run_wrapper const&) = delete;
    run_wrapper(run_wrapper&&) = delete;
    run_wrapper& operator=(run_wrapper const&) = delete;
    run_wrapper& operator=(run_wrapper&&) = delete;

    template<ContextRunnable Task>
    [[nodiscard]] auto operator()(Task t) &&
    {
        // The task's frame is made; the caller's own frames are not.
        set_current_frame_allocator(saved_tls_);
        restored_ = true;
        if constexpr (InheritStopToken)
            return run_awaitable<Task, true, Alloc>{
                std::move(alloc_), std::move(t)};
        else
            return run_awaitable<Task, false, Alloc>{
                std::move(alloc_), std::move(t), std::move(st_)};
    }
};

/// Specialization for memory_resource* - stores pointer directly.
template<bool InheritStopToken>
class [[nodiscard]] run_wrapper<InheritStopToken, std::pmr::memory_resource*>
{
    std::conditional_t<InheritStopToken, std::monostate, std::stop_token> st_;
    std::pmr::memory_resource* mr_;

    std::pmr::memory_resource* saved_tls_ =
        get_current_frame_allocator();
    bool restored_ = false;

public:
    explicit run_wrapper(std::pmr::memory_resource* mr)
        requires InheritStopToken
        : mr_(mr)
    {
        set_current_frame_allocator(mr_);
    }

    run_wrapper(std::stop_token st, std::pmr::memory_resource* mr)
        requires (!InheritStopToken)
        : st_(std::move(st))
        , mr_(mr)
    {
        set_current_frame_allocator(mr_);
    }

    // Restores the caller's allocator when evaluating the task argument
    // threw. Not afterwards: a wrapper destroyed with a torn-down parent
    // frame must not touch the tearing thread's allocator.
    ~run_wrapper()
    {
        if(!restored_)
            set_current_frame_allocator(saved_tls_);
    }

    // Non-copyable, non-movable (must be used immediately)
    run_wrapper(run_wrapper const&) = delete;
    run_wrapper(run_wrapper&&) = delete;
    run_wrapper& operator=(run_wrapper const&) = delete;
    run_wrapper& operator=(run_wrapper&&) = delete;

    template<ContextRunnable Task>
    [[nodiscard]] auto operator()(Task t) &&
    {
        // The task's frame is made; the caller's own frames are not.
        set_current_frame_allocator(saved_tls_);
        restored_ = true;
        if constexpr (InheritStopToken)
            return run_awaitable<Task, true, std::pmr::memory_resource*>{
                mr_, std::move(t)};
        else
            return run_awaitable<Task, false, std::pmr::memory_resource*>{
                mr_, std::move(t), std::move(st_)};
    }
};

/// Specialization for stop_token only (no allocator).
template<>
class [[nodiscard]] run_wrapper<false, void>
{
    std::stop_token st_;

public:
    explicit run_wrapper(std::stop_token st)
        : st_(std::move(st))
    {
    }

    // Non-copyable, non-movable (must be used immediately)
    run_wrapper(run_wrapper const&) = delete;
    run_wrapper(run_wrapper&&) = delete;
    run_wrapper& operator=(run_wrapper const&) = delete;
    run_wrapper& operator=(run_wrapper&&) = delete;

    template<ContextRunnable Task>
    [[nodiscard]] auto operator()(Task t) &&
    {
        return run_awaitable<Task, false, void>{std::move(t), std::move(st_)};
    }
};

} // namespace boost::capy::detail

namespace boost::capy {

/** Bind a task to execute on a specific executor.

    Returns a wrapper that accepts a task and produces an awaitable.
    When co_awaited, the task runs on the specified executor.

    If the inner task is abandoned before it completes, for example
    because its execution context is torn down, the caller's chain is
    abandoned too: it is unwound by its owner, on the owner's executor,
    without the caller resuming. The awaitable holds work on the
    caller's executor until the caller resumes or is unwound.

    @par Preconditions
    The caller's execution context outlives the inner task and the
    unwind of the caller's chain. This is not detected.

    @par Example
    @par !example example_2


    @param ex The executor on which the task should run.

    @return A wrapper that accepts a task for execution.

    @see task
    @see Executor
*/
template<Executor Ex>
[[nodiscard]] auto
run(Ex ex)
{
    return detail::run_wrapper_ex<Ex, true, void>{std::move(ex)};
}

/** Bind a task to an executor with a stop token.

    @param ex The executor on which the task should run.
    @param st The stop token for cooperative cancellation.

    @return A wrapper that accepts a task for execution.

    @see run(Ex), which describes teardown and its precondition.
*/
template<Executor Ex>
[[nodiscard]] auto
run(Ex ex, std::stop_token st)
{
    return detail::run_wrapper_ex<Ex, false, void>{
        std::move(ex), std::move(st)};
}

/** Bind a task to an executor with a memory resource.

    @param ex The executor on which the task should run.
    @param mr The memory resource for frame allocation.

    @return A wrapper that accepts a task for execution.

    @see run(Ex), which describes teardown and its precondition.
*/
template<Executor Ex>
[[nodiscard]] auto
run(Ex ex, std::pmr::memory_resource* mr)
{
    return detail::run_wrapper_ex<Ex, true, std::pmr::memory_resource*>{
        std::move(ex), mr};
}

/** Bind a task to an executor with a standard allocator.

    Await the awaitable in the expression that creates it, as in
    `co_await run(ex, alloc)(task())`: the task's frame is allocated from
    memory the wrapper holds until the end of that expression.

    @param ex The executor on which the task should run.
    @param alloc The allocator for frame allocation.

    @return A wrapper that accepts a task for execution.

    @see run(Ex), which describes teardown and its precondition.
*/
template<Executor Ex, detail::Allocator Alloc>
[[nodiscard]] auto
run(Ex ex, Alloc alloc)
{
    return detail::run_wrapper_ex<Ex, true, Alloc>{
        std::move(ex), std::move(alloc)};
}

/** Bind a task to an executor with stop token and memory resource.

    @param ex The executor on which the task should run.
    @param st The stop token for cooperative cancellation.
    @param mr The memory resource for frame allocation.

    @return A wrapper that accepts a task for execution.

    @see run(Ex), which describes teardown and its precondition.
*/
template<Executor Ex>
[[nodiscard]] auto
run(Ex ex, std::stop_token st, std::pmr::memory_resource* mr)
{
    return detail::run_wrapper_ex<Ex, false, std::pmr::memory_resource*>{
        std::move(ex), std::move(st), mr};
}

/** Bind a task to an executor with stop token and standard allocator.

    Await the awaitable in the expression that creates it, as in
    `co_await run(ex, alloc)(task())`: the task's frame is allocated from
    memory the wrapper holds until the end of that expression.

    @param ex The executor on which the task should run.
    @param st The stop token for cooperative cancellation.
    @param alloc The allocator for frame allocation.

    @return A wrapper that accepts a task for execution.

    @see run(Ex), which describes teardown and its precondition.
*/
template<Executor Ex, detail::Allocator Alloc>
[[nodiscard]] auto
run(Ex ex, std::stop_token st, Alloc alloc)
{
    return detail::run_wrapper_ex<Ex, false, Alloc>{
        std::move(ex), std::move(st), std::move(alloc)};
}

/** Run a task with a custom stop token.

    The task inherits the caller's executor. Only the stop token
    is overridden.

    @par Example
    @par !example example_1


    @param st The stop token for cooperative cancellation.

    @return A wrapper that accepts a task for execution.
*/
[[nodiscard]] inline auto
run(std::stop_token st)
{
    return detail::run_wrapper<false, void>{std::move(st)};
}

/** Run a task with a custom memory resource.

    The task inherits the caller's executor. The memory resource
    is used for nested frame allocations.

    @param mr The memory resource for frame allocation.

    @return A wrapper that accepts a task for execution.
*/
[[nodiscard]] inline auto
run(std::pmr::memory_resource* mr)
{
    return detail::run_wrapper<true, std::pmr::memory_resource*>{mr};
}

// MrDocs 0.8.0 reports `run: Documented parameter 'alloc' does not exist` and
// pins it to the run(Ex) overload near the top of this namespace:
// `detail::Allocator` is implementation-defined, so the constraint is erased
// and the two single-parameter `run` overloads collapse onto one page. The
// finding is grandfathered in doc/lint/baseline.json. Deleting the `@param
// alloc` below clears it and produces no replacement finding, because MrDocs
// has no model of this overload at all -- so it would silently drop real
// documentation. Keep the `@param`.
/** Run a task with a custom standard allocator.

    Await the awaitable in the expression that creates it, as in
    `co_await run(alloc)(task())`: the task's frame is allocated from
    memory the wrapper holds until the end of that expression.

    The task inherits the caller's executor. The allocator is used
    for nested frame allocations.

    @param alloc The allocator for frame allocation.

    @return A wrapper that accepts a task for execution.
*/
template<detail::Allocator Alloc>
[[nodiscard]] auto
run(Alloc alloc)
{
    return detail::run_wrapper<true, Alloc>{std::move(alloc)};
}

/** Run a task with stop token and memory resource.

    The task inherits the caller's executor.

    @param st The stop token for cooperative cancellation.
    @param mr The memory resource for frame allocation.

    @return A wrapper that accepts a task for execution.
*/
[[nodiscard]] inline auto
run(std::stop_token st, std::pmr::memory_resource* mr)
{
    return detail::run_wrapper<false, std::pmr::memory_resource*>{
        std::move(st), mr};
}

/** Run a task with stop token and standard allocator.

    Await the awaitable in the expression that creates it, as in
    `co_await run(alloc)(task())`: the task's frame is allocated from
    memory the wrapper holds until the end of that expression.

    The task inherits the caller's executor.

    @param st The stop token for cooperative cancellation.
    @param alloc The allocator for frame allocation.

    @return A wrapper that accepts a task for execution.
*/
template<detail::Allocator Alloc>
[[nodiscard]] auto
run(std::stop_token st, Alloc alloc)
{
    return detail::run_wrapper<false, Alloc>{
        std::move(st), std::move(alloc)};
}

} // namespace boost::capy

#endif
