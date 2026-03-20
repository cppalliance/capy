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
#include <boost/capy/concept/io_runnable.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <coroutine>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/io_env.hpp>

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

    The evaluation order is:
        1. run(ex, alloc) creates a temporary wrapper
        2. my_task() allocates its coroutine frame using TLS
        3. operator() returns an awaitable
        4. Wrapper temporary is DESTROYED
        5. co_await suspends caller, resumes task
        6. Task body executes (wrapper is already dead!)

    Problem: The wrapper's frame_memory_resource dies before the task
    body runs. When initial_suspend::await_resume() restores TLS from
    the saved pointer, it would point to dead memory.

    Solution: Store a COPY of the allocator in the awaitable (not just
    the wrapper). The co_await mechanism extends the awaitable's lifetime
    until the await completes. In await_suspend, we overwrite the promise's
    saved frame_allocator pointer to point to the awaitable's resource.

    This works because standard allocator copies are equivalent - memory
    allocated with one copy can be deallocated with another copy. The
    task's own frame uses the footer-stored pointer (safe), while nested
    task creation uses TLS pointing to the awaitable's resource (also safe).
*/

namespace boost::capy::detail {

/** Minimal coroutine that dispatches through the caller's executor.

    Sits between the inner task and the parent when executors
    diverge. The inner task's `final_suspend` resumes this
    trampoline via symmetric transfer. The trampoline's own
    `final_suspend` dispatches the parent through the caller's
    executor to restore the correct execution context.

    The trampoline never touches the task's result.
*/
struct dispatch_trampoline
{
    struct promise_type
    {
        executor_ref caller_ex_;
        continuation parent_;

        dispatch_trampoline get_return_object() noexcept
        {
            return dispatch_trampoline{
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
                    return detail::symmetric_transfer(
                        p_->caller_ex_.dispatch(p_->parent_));
                }

                void await_resume() const noexcept {}
            };
            return awaiter{this};
        }

        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };

    std::coroutine_handle<promise_type> h_{nullptr};

    dispatch_trampoline() noexcept = default;

    ~dispatch_trampoline()
    {
        if(h_) h_.destroy();
    }

    dispatch_trampoline(dispatch_trampoline const&) = delete;
    dispatch_trampoline& operator=(dispatch_trampoline const&) = delete;

    dispatch_trampoline(dispatch_trampoline&& o) noexcept
        : h_(std::exchange(o.h_, nullptr)) {}

    dispatch_trampoline& operator=(dispatch_trampoline&& o) noexcept
    {
        if(this != &o)
        {
            if(h_) h_.destroy();
            h_ = std::exchange(o.h_, nullptr);
        }
        return *this;
    }

private:
    explicit dispatch_trampoline(std::coroutine_handle<promise_type> h) noexcept
        : h_(h) {}
};

inline dispatch_trampoline make_dispatch_trampoline()
{
    co_return;
}

/** Awaitable that binds an IoRunnable to a specific executor.

    Stores the executor and inner task by value. When co_awaited, the
    co_await expression's lifetime extension keeps both alive for the
    duration of the operation.

    A dispatch trampoline handles the executor switch on completion:
    the inner task's `final_suspend` resumes the trampoline, which
    dispatches back through the caller's executor.

    The `io_env` is owned by this awaitable and is guaranteed to
    outlive the inner task and all awaitables in its chain. Awaitables
    may store `io_env const*` without concern for dangling references.

    @tparam Task The IoRunnable type
    @tparam Ex The executor type
    @tparam InheritStopToken If true, inherit caller's stop token
    @tparam Alloc The allocator type (void for no allocator)
*/
template<IoRunnable Task, Executor Ex, bool InheritStopToken, class Alloc = void>
struct [[nodiscard]] run_awaitable_ex
{
    Ex ex_;
    frame_memory_resource<Alloc> resource_;
    std::conditional_t<InheritStopToken, std::monostate, std::stop_token> st_;
    io_env env_;
    dispatch_trampoline tr_;
    continuation task_cont_;
    Task inner_;  // Last: destroyed first, while env_ is still valid

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
        return inner_.await_resume();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont, io_env const* caller_env)
    {
        tr_ = make_dispatch_trampoline();
        tr_.h_.promise().caller_ex_ = caller_env->executor;
        tr_.h_.promise().parent_.h = cont;

        auto h = inner_.handle();
        auto& p = h.promise();
        p.set_continuation(tr_.h_);

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
        return ex_.dispatch(task_cont_);
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

    @tparam Task The IoRunnable type
    @tparam InheritStopToken If true, inherit caller's stop token
    @tparam Alloc The allocator type (void for no allocator)
*/
template<IoRunnable Task, bool InheritStopToken, class Alloc = void>
struct [[nodiscard]] run_awaitable
{
    frame_memory_resource<Alloc> resource_;
    std::conditional_t<InheritStopToken, std::monostate, std::stop_token> st_;
    io_env env_;
    Task inner_;  // Last: destroyed first, while env_ is still valid

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
        return inner_.await_resume();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont, io_env const* caller_env)
    {
        auto h = inner_.handle();
        auto& p = h.promise();
        p.set_continuation(cont);

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

    // Non-copyable, non-movable (must be used immediately)
    run_wrapper_ex(run_wrapper_ex const&) = delete;
    run_wrapper_ex(run_wrapper_ex&&) = delete;
    run_wrapper_ex& operator=(run_wrapper_ex const&) = delete;
    run_wrapper_ex& operator=(run_wrapper_ex&&) = delete;

    template<IoRunnable Task>
    [[nodiscard]] auto operator()(Task t) &&
    {
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

    // Non-copyable, non-movable (must be used immediately)
    run_wrapper_ex(run_wrapper_ex const&) = delete;
    run_wrapper_ex(run_wrapper_ex&&) = delete;
    run_wrapper_ex& operator=(run_wrapper_ex const&) = delete;
    run_wrapper_ex& operator=(run_wrapper_ex&&) = delete;

    template<IoRunnable Task>
    [[nodiscard]] auto operator()(Task t) &&
    {
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

    template<IoRunnable Task>
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

    // Non-copyable, non-movable (must be used immediately)
    run_wrapper(run_wrapper const&) = delete;
    run_wrapper(run_wrapper&&) = delete;
    run_wrapper& operator=(run_wrapper const&) = delete;
    run_wrapper& operator=(run_wrapper&&) = delete;

    template<IoRunnable Task>
    [[nodiscard]] auto operator()(Task t) &&
    {
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

    // Non-copyable, non-movable (must be used immediately)
    run_wrapper(run_wrapper const&) = delete;
    run_wrapper(run_wrapper&&) = delete;
    run_wrapper& operator=(run_wrapper const&) = delete;
    run_wrapper& operator=(run_wrapper&&) = delete;

    template<IoRunnable Task>
    [[nodiscard]] auto operator()(Task t) &&
    {
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

    template<IoRunnable Task>
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

    @par Example
    @code
    co_await run(other_executor)(my_task());
    @endcode

    @param ex The executor on which the task should run.

    @return A wrapper that accepts a task for execution.

    @see task
    @see executor
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
*/
template<Executor Ex>
[[nodiscard]] auto
run(Ex ex, std::pmr::memory_resource* mr)
{
    return detail::run_wrapper_ex<Ex, true, std::pmr::memory_resource*>{
        std::move(ex), mr};
}

/** Bind a task to an executor with a standard allocator.

    @param ex The executor on which the task should run.
    @param alloc The allocator for frame allocation.

    @return A wrapper that accepts a task for execution.
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
*/
template<Executor Ex>
[[nodiscard]] auto
run(Ex ex, std::stop_token st, std::pmr::memory_resource* mr)
{
    return detail::run_wrapper_ex<Ex, false, std::pmr::memory_resource*>{
        std::move(ex), std::move(st), mr};
}

/** Bind a task to an executor with stop token and standard allocator.

    @param ex The executor on which the task should run.
    @param st The stop token for cooperative cancellation.
    @param alloc The allocator for frame allocation.

    @return A wrapper that accepts a task for execution.
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
    @code
    std::stop_source source;
    co_await run(source.get_token())(cancellable_task());
    @endcode

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

/** Run a task with a custom standard allocator.

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
