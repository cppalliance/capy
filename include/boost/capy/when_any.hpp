//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_WHEN_ANY_HPP
#define BOOST_CAPY_WHEN_ANY_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/io_awaitable.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/task.hpp>

#include <array>
#include <atomic>
#include <exception>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

/*
 * when_any - Race multiple tasks, return first completion
 * ========================================================
 *
 * OVERVIEW:
 * ---------
 * when_any launches N tasks concurrently and completes when the FIRST task
 * finishes (success or failure). It then requests stop for all siblings and
 * waits for them to acknowledge before returning.
 *
 * ARCHITECTURE:
 * -------------
 * The design mirrors when_all but with inverted completion semantics:
 *
 *   when_all:  complete when remaining_count reaches 0 (all done)
 *   when_any:  complete when has_winner becomes true (first done)
 *              BUT still wait for remaining_count to reach 0 for cleanup
 *
 * Key components:
 *   - when_any_state:    Shared state tracking winner and completion
 *   - when_any_runner:   Wrapper coroutine for each child task
 *   - when_any_launcher: Awaitable that starts all runners concurrently
 *
 * CRITICAL INVARIANTS:
 * --------------------
 * 1. Exactly one task becomes the winner (via atomic compare_exchange)
 * 2. All tasks must complete before parent resumes (cleanup safety)
 * 3. Stop is requested immediately when winner is determined
 * 4. Only the winner's result/exception is stored
 *
 * TYPE DEDUPLICATION:
 * -------------------
 * std::variant requires unique alternative types. Since when_any can race
 * tasks with identical return types (e.g., three task<int>), we must
 * deduplicate types before constructing the variant.
 *
 * Example: when_any(task<int>, task<string>, task<int>)
 *   - Raw types after void->monostate: int, string, int
 *   - Deduplicated variant: std::variant<int, string>
 *   - Return: pair<size_t, variant<int, string>>
 *
 * The winner_index tells you which task won (0, 1, or 2), while the variant
 * holds the result. Use the index to determine how to interpret the variant.
 *
 * VOID HANDLING:
 * --------------
 * void tasks contribute std::monostate to the variant (then deduplicated).
 * All-void tasks result in: pair<size_t, variant<monostate>>
 *
 * MEMORY MODEL:
 * -------------
 * - try_win() uses acq_rel to synchronize winner selection
 * - signal_completion() uses acq_rel for remaining_count
 * - Winner data (result/exception) is written before try_win() returns true,
 *   and read after all tasks complete, so no additional synchronization needed
 *
 * EXCEPTION SEMANTICS:
 * --------------------
 * Unlike when_all (which captures first exception, discards others), when_any
 * treats exceptions as valid completions. If the winning task threw, that
 * exception is rethrown. Exceptions from non-winners are silently discarded.
 */

namespace boost {
namespace capy {

namespace detail {

/** Convert void to monostate for variant storage.

    std::variant<void, ...> is ill-formed, so void tasks contribute
    std::monostate to the result variant instead. Non-void types
    pass through unchanged.

    @tparam T The type to potentially convert (void becomes monostate).
*/
template<typename T>
using void_to_monostate_t = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

/** Type deduplication for variant construction.

    std::variant requires unique alternative types. These metafunctions
    deduplicate a type list while preserving order of first occurrence.

    @par Algorithm
    Fold left over the type list, appending each type to the accumulator
    only if not already present. O(N^2) in number of types but N is
    typically small (number of when_any arguments).
*/
/** Primary template for appending a type to a variant if not already present.

    @tparam Variant The accumulated variant type.
    @tparam T The type to potentially append.
*/
template<typename Variant, typename T>
struct variant_append_if_unique;

/** Specialization that checks for type uniqueness and appends if needed.

    @tparam Vs Types already in the variant.
    @tparam T The type to potentially append.
*/
template<typename... Vs, typename T>
struct variant_append_if_unique<std::variant<Vs...>, T>
{
    /** Result type: original variant if T is duplicate, extended variant otherwise. */
    using type = std::conditional_t<
        (std::is_same_v<T, Vs> || ...),
        std::variant<Vs...>,
        std::variant<Vs..., T>>;
};

/** Primary template for type list deduplication.

    @tparam Accumulated The variant accumulating unique types.
    @tparam Remaining Types still to be processed.
*/
template<typename Accumulated, typename... Remaining>
struct deduplicate_impl;

/** Base case: no more types to process.

    @tparam Accumulated The final deduplicated variant type.
*/
template<typename Accumulated>
struct deduplicate_impl<Accumulated>
{
    /** The final deduplicated variant type. */
    using type = Accumulated;
};

/** Recursive case: add T if unique, then process rest.

    @tparam Accumulated The variant accumulated so far.
    @tparam T The current type to potentially add.
    @tparam Rest Remaining types to process.
*/
template<typename Accumulated, typename T, typename... Rest>
struct deduplicate_impl<Accumulated, T, Rest...>
{
    /** Intermediate type after potentially appending T. */
    using next = typename variant_append_if_unique<Accumulated, T>::type;

    /** Final result after processing all remaining types. */
    using type = typename deduplicate_impl<next, Rest...>::type;
};

/** Deduplicated variant from a list of types.

    Constructs a std::variant containing unique types from the input list.
    Void types are converted to std::monostate before deduplication.
    The first type T0 seeds the accumulator, ensuring the variant is well-formed.

    @tparam T0 First result type (required, seeds the deduplication).
    @tparam Ts Remaining result types (void is converted to monostate).
*/
template<typename T0, typename... Ts>
using unique_variant_t = typename deduplicate_impl<
    std::variant<void_to_monostate_t<T0>>,
    void_to_monostate_t<Ts>...>::type;

/** Result type for when_any: (winner_index, deduplicated_variant).

    The first element is the zero-based index of the winning task in the
    original argument order. The second element is a variant holding the
    winner's result by type. When multiple tasks share the same return type,
    use the index to determine which task actually won.

    @tparam T0 First task's result type.
    @tparam Ts Remaining tasks' result types.
*/
template<typename T0, typename... Ts>
using when_any_result_t = std::pair<std::size_t, unique_variant_t<T0, Ts...>>;

/** Shared state for when_any operation.

    Coordinates winner selection, result storage, and completion tracking
    for all child tasks in a when_any operation.

    @par Lifetime
    Allocated on the parent coroutine's frame, outlives all runners.

    @par Thread Safety
    Atomic operations protect winner selection and completion count.
    Result storage is written only by the winner before any concurrent access.

    @tparam T0 First task's result type.
    @tparam Ts Remaining tasks' result types.
*/
template<typename T0, typename... Ts>
struct when_any_state
{
    /** Total number of tasks being raced. */
    static constexpr std::size_t task_count = 1 + sizeof...(Ts);

    /** Deduplicated variant type for storing the winner's result. */
    using variant_type = unique_variant_t<T0, Ts...>;

    /** Counter for tasks still running.

        Must wait for ALL tasks to finish before parent resumes; this ensures
        runner coroutine frames are valid until their final_suspend completes.
    */
    std::atomic<std::size_t> remaining_count_;

    /** Flag indicating whether a winner has been determined.

        Winner selection: exactly one task wins via atomic CAS on has_winner_.
        winner_index_ is written only by the winner, read after all complete.
    */
    std::atomic<bool> has_winner_{false};

    /** Index of the winning task in the original argument list. */
    std::size_t winner_index_{0};

    /** Storage for the winner's result value.

        Result storage: deduplicated variant. Stored by type, not task index,
        because multiple tasks may share the same return type.
    */
    variant_type result_;

    /** Exception thrown by the winner, if any.

        Non-null if winner threw (rethrown to caller after all tasks complete).
    */
    std::exception_ptr winner_exception_;

    /** Handles to runner coroutines for cleanup.

        Runner coroutine handles; destroyed in destructor after all complete.
    */
    std::array<coro, task_count> runner_handles_{};

    /** Stop source for cancelling sibling tasks.

        Owned stop_source: request_stop() called when winner determined.
    */
    std::stop_source stop_source_;

    /** Callback functor that forwards stop requests.

        Forwards parent's stop requests to our stop_source, enabling
        cancellation to propagate from caller through when_any to children.
    */
    struct stop_callback_fn
    {
        /** Pointer to the stop source to signal. */
        std::stop_source* source_;

        /**
 * @brief Request cancellation on the associated stop source.
 *
 * Invokes `request_stop()` on the underlying `std::stop_source`.
 */
        void operator()() const noexcept { source_->request_stop(); }
    };

    /** Type alias for the stop callback registration. */
    using stop_callback_t = std::stop_callback<stop_callback_fn>;

    /** Optional callback linking parent's stop token to our stop source. */
    std::optional<stop_callback_t> parent_stop_callback_;

    /** Parent coroutine handle to resume when all children complete. */
    coro continuation_;

    /** Executor reference for dispatching the parent resumption. */
    executor_ref caller_ex_;

    /** Construct state for racing task_count tasks.

        Initializes remaining_count_ to task_count so all tasks must complete
        before the parent coroutine resumes.
    */
    when_any_state()
        : remaining_count_(task_count)
    {
    }

    /** Destroy state and clean up runner coroutine handles.

        All runners must have completed before destruction (guaranteed by
        waiting for remaining_count_ to reach zero).
    */
    ~when_any_state()
    {
        for(auto h : runner_handles_)
            if(h)
                h.destroy();
    }

    /** Attempt to become the winner.

        Atomically claims winner status. Exactly one task succeeds; all others
        see false. The winner must store its result before returning.

        @param index The task's index in the original argument list.
        @return true if this task is now the winner, false if another won first.
    */
    bool try_win(std::size_t index) noexcept
    {
        bool expected = false;
        if(has_winner_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        {
            winner_index_ = index;
            // Signal siblings to exit early if they support cancellation
            stop_source_.request_stop();
            return true;
        }
        return false;
    }

    /** Store the winner's result.

        @pre Only called by the winner (try_win returned true).
        @note Uses type-based emplacement because the variant is deduplicated;
              task index may differ from variant alternative index.
    */
    template<typename T>
    /**
     * @brief Store the winner's result into the shared deduplicated variant.
     *
     * Moves the provided value into the state's result variant under the alternative
     * corresponding to its type.
     *
     * @param value The winner's result to store; will be moved into the variant.
     */
    void set_winner_result(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        result_.template emplace<T>(std::move(value));
    }

    /**
     * @brief Store the winner's completion value for a void-returning task as std::monostate.
     *
     * @pre Called only by the runner that won the race.
     */
    void set_winner_void() noexcept
    {
        result_.template emplace<std::monostate>(std::monostate{});
    }

    /**
     * Stores the winning task's exception for later inspection or rethrow.
     *
     * @param ep Exception pointer captured from the winning task.
     * @pre This is called only by the winner (i.e., after try_win returned true).
     */
    void set_winner_exception(std::exception_ptr ep) noexcept
    {
        winner_exception_ = ep;
    }

    /**
     * @brief Signals that a runner has finished and, if it was the last, resumes the parent coroutine.
     *
     * Called by each runner's final_suspend. Decrements the remaining count and, when the
     * last runner completes, dispatches the stored continuation via the caller executor so
     * the parent resumes after all child frames are destroyed.
     *
     * @return Coroutine to resume: the parent's continuation if this call observed the final completion, otherwise a noop coroutine.
     */
    coro signal_completion() noexcept
    {
        auto remaining = remaining_count_.fetch_sub(1, std::memory_order_acq_rel);
        if(remaining == 1)
            return caller_ex_.dispatch(continuation_);
        return std::noop_coroutine();
    }
};

/** Wrapper coroutine that runs a single child task for when_any.

    Each child task is wrapped in a runner that:
    1. Propagates executor and stop_token to the child
    2. Attempts to claim winner status on completion
    3. Stores result only if this runner won
    4. Signals completion regardless of win/loss (for cleanup)

    @tparam T The result type of the wrapped task.
    @tparam Ts All task result types (for when_any_state compatibility).
*/
template<typename T, typename... Ts>
struct when_any_runner
{
    /** Promise type for the runner coroutine.

        Manages executor propagation, stop token forwarding, and completion
        signaling for the wrapped child task.
    */
    struct promise_type : frame_allocating_base
    {
        /** Pointer to shared state for winner coordination. */
        when_any_state<Ts...>* state_ = nullptr;

        /** Index of this task in the original argument list. */
        std::size_t index_ = 0;

        /** Executor reference inherited from the parent coroutine. */
        executor_ref ex_;

        /** Stop token for cooperative cancellation. */
        std::stop_token stop_token_;

        /**
         * @brief Construct a runner object that owns the coroutine associated with this promise.
         *
         * @return when_any_runner A runner wrapping the coroutine handle obtained from this promise.
         */
        when_any_runner get_return_object()
        {
            return when_any_runner(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        /**
         * @brief Suspend the coroutine immediately upon creation.
         *
         * @return An awaiter that always suspends the coroutine.
         */
        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        /**
         * @brief Final-suspend awaiter that notifies shared state this runner has finished.
         *
         * The returned awaiter always suspends; on suspend it calls the shared state's
         * signal_completion() to decrement the remaining task count and obtain the
         * coroutine to resume (the parent continuation) if this was the last runner.
         *
         * @return The parent coroutine to resume if this was the last task, `std::noop_coroutine()` otherwise.
         */
        auto final_suspend() noexcept
        {
            /** Awaiter that signals task completion and potentially resumes parent. */
            struct awaiter
            {
                /** Pointer to the promise for accessing shared state. */
                promise_type* p_;

                /**
                 * @brief Indicates the awaiter is never ready and always suspends.
                 *
                 * @return `false` — always suspend the awaiting coroutine.
                 */
                bool await_ready() const noexcept
                {
                    return false;
                }

                /**
                 * Notify the shared when_any state that this runner has completed and obtain the coroutine to resume.
                 *
                 * @return The parent coroutine to resume if this was the last outstanding task; otherwise `std::noop_coroutine()`.
                 */
                coro await_suspend(coro) noexcept
                {
                    return p_->state_->signal_completion();
                }

                /**
                 * @brief No-op await_resume for the final_suspend awaiter.
                 *
                 * Performs no action when resumed; control simply returns to the awaiting context.
                 */
                void await_resume() const noexcept
                {
                }
            };
            return awaiter{this};
        }

        /**
         * @brief Satisfies the coroutine promise's return requirement when a runner completes successfully.
         *
         * @details No-op used to indicate normal completion of the runner coroutine. Result capture and
         * propagation are performed by the runner/launcher infrastructure; this function itself performs no work.
         */
        void return_void()
        {
        }

        /**
         * @brief Handle an exception thrown by the child runner.
         *
         * Exceptions are treated as valid completions. If this runner's exception
         * becomes the winning result it is stored for rethrowing to the caller once
         * all runners complete; otherwise the exception is discarded.
         */
        void unhandled_exception()
        {
            if(state_->try_win(index_))
                state_->set_winner_exception(std::current_exception());
        }

        /** Awaiter wrapper that injects executor and stop token into child awaitables.

            @tparam Awaitable The underlying awaitable type being wrapped.
        */
        template<class Awaitable>
        struct transform_awaiter
        {
            /** The wrapped awaitable instance. */
            std::decay_t<Awaitable> a_;

            /** Pointer to promise for accessing executor and stop token. */
            promise_type* p_;

            /**
             * Indicates whether the underlying awaitable can complete synchronously.
             *
             * @return `true` if the awaitable can complete synchronously, `false` otherwise.
             */
            bool await_ready()
            {
                return a_.await_ready();
            }

            /**
             * @brief Resume the underlying awaitable and obtain its result.
             *
             * @return The value returned by the wrapped awaitable's `await_resume`.
             */
            auto await_resume()
            {
                return a_.await_resume();
            }

            /** Suspend with executor and stop token injection.

                @tparam Promise The suspending coroutine's promise type.
                @param h Handle to the suspending coroutine.
                @return Coroutine to resume or void.
            */
            template<class Promise>
            /**
             * @brief Forwards suspension to the wrapped awaitable while injecting the promise's executor and stop token.
             *
             * @param h Coroutine handle of the awaiting coroutine.
             * @return The value returned by the underlying awaitable's `await_suspend` call.
             */
            auto await_suspend(std::coroutine_handle<Promise> h)
            {
                return a_.await_suspend(h, p_->ex_, p_->stop_token_);
            }
        };

        /** Transform awaitables to inject executor and stop token.

            @tparam Awaitable The awaitable type being co_awaited.
            @param a The awaitable instance.
            @return Transformed awaiter with executor/stop_token injection.
        */
        template<class Awaitable>
        /**
         * @brief Adapts an awaitable to the runner promise's execution context.
         *
         * Produces an awaitable that will run with this promise's executor and stop token:
         * if the awaited type accepts an executor and stop token, the returned awaitable
         * forwards this promise's executor and stop token to the inner operation;
         * otherwise the returned awaitable is bound to this promise's executor.
         *
         * @tparam Awaitable Type of the awaitable being adapted.
         * @param a The awaitable to adapt.
         * @return An awaitable adapted to execute with the promise's executor and stop token.
         */
        auto await_transform(Awaitable&& a)
        {
            using A = std::decay_t<Awaitable>;
            if constexpr (IoAwaitable<A, executor_ref>)
            {
                return transform_awaiter<Awaitable>{
                    std::forward<Awaitable>(a), this};
            }
            else
            {
                return make_affine(std::forward<Awaitable>(a), ex_);
            }
        }
    };

    /** Handle to the underlying coroutine frame. */
    std::coroutine_handle<promise_type> h_;

    /**
     * @brief Construct a runner wrapper from an existing coroutine handle.
     *
     * @param h Coroutine handle for the runner promise; ownership of the handle is stored by the returned wrapper.
     */
    explicit when_any_runner(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }

    /** Move constructor (Clang 14 workaround).

        Clang 14 (non-Apple) has a coroutine codegen bug requiring explicit
        move constructor; other compilers work correctly with deleted move.
    */
#if defined(__clang__) && __clang_major__ == 14 && !defined(__apple_build_version__)
    /**
 * @brief Move-constructs a runner by transferring ownership of the underlying coroutine handle.
 *
 * The source runner's handle is set to `nullptr` after the transfer.
 *
 * @param other Source runner whose coroutine handle will be moved-from and nulled.
 */
when_any_runner(when_any_runner&& other) noexcept : h_(std::exchange(other.h_, nullptr)) {}
#endif

    /** Copy construction is not allowed. */
    when_any_runner(when_any_runner const&) = delete;

    /** Copy assignment is not allowed. */
    when_any_runner& operator=(when_any_runner const&) = delete;

    /** Move construction is deleted (except on Clang 14). */
#if !defined(__clang__) || __clang_major__ != 14 || defined(__apple_build_version__)
    /**
 * @brief Deleted move constructor; instances of this type cannot be moved.
 *
 * Attempts to move a `when_any_runner` are prohibited to preserve unique coroutine handle ownership and invariants.
 */
when_any_runner(when_any_runner&&) = delete;
#endif

    /** Move assignment is not allowed. */
    when_any_runner& operator=(when_any_runner&&) = delete;

    /** Release ownership of the coroutine handle.

        @return The coroutine handle; this object becomes empty.
    */
    auto release() noexcept
    {
        return std::exchange(h_, nullptr);
    }
};

/** Create a runner coroutine for a single task in when_any.

    Factory function that creates a wrapper coroutine for a child task.
    The runner handles executor/stop_token propagation and winner selection.

    @tparam Index Compile-time index of this task in the argument list.
    @tparam T The result type of the task being wrapped.
    @tparam Ts All task result types (for when_any_state compatibility).
    @param inner The task to run (will be moved from).
    @param state Shared state for winner coordination.
    @return Runner coroutine (must be started via resume()).
*/
template<std::size_t Index, typename T, typename... Ts>
/**
 * @brief Creates a runner coroutine that awaits a child task and, if it completes first, records its outcome into shared when_any state.
 *
 * The returned runner will await the provided task `inner`. If the awaited task becomes the winner, the runner stores the winner's value (or a monostate for `void`) or captures and stores the winner's exception in `state`.
 *
 * @tparam Index Index of this task within the when_any launch set.
 * @tparam T Type returned by the `inner` task.
 * @tparam Ts Remaining types in the when_any state.
 * @param inner The child task to run.
 * @param state Pointer to the shared when_any_state coordinating winner selection and result storage.
 * @return when_any_runner<T, Ts...> A coroutine wrapper that runs `inner` and updates `state` on completion.
 */
when_any_runner<T, Ts...>
make_when_any_runner(task<T> inner, when_any_state<Ts...>* state)
{
    if constexpr (std::is_void_v<T>)
    {
        co_await std::move(inner);
        if(state->try_win(Index))
            state->set_winner_void();  // noexcept
    }
    else
    {
        auto result = co_await std::move(inner);
        if(state->try_win(Index))
        {
            try
            {
                state->set_winner_result(std::move(result));
            }
            catch(...)
            {
                state->set_winner_exception(std::current_exception());
            }
        }
    }
}

/** Awaitable that launches all runner coroutines concurrently.

    Handles the tricky lifetime issue where tasks may complete synchronously
    during launch, potentially destroying this awaitable's frame before
    all tasks are extracted from the tuple. See await_suspend for details.

    @tparam Ts The result types of the tasks being launched.
*/
template<typename... Ts>
class when_any_launcher
{
    /** Pointer to tuple of tasks to launch. */
    std::tuple<task<Ts>...>* tasks_;

    /** Pointer to shared state for coordination. */
    when_any_state<Ts...>* state_;

public:
    /** Construct launcher with task tuple and shared state.

        @param tasks Pointer to tuple of tasks (must outlive the await).
        @param state Pointer to shared state for winner coordination.
    */
    when_any_launcher(
        std::tuple<task<Ts>...>* tasks,
        when_any_state<Ts...>* state)
        : tasks_(tasks)
        , state_(state)
    {
    }

    /**
     * @brief Determines whether the launcher completes immediately.
     *
     * The launcher is ready if there are no tasks to launch.
     *
     * @return `true` if there are zero tasks, `false` otherwise.
     */
    bool await_ready() const noexcept
    {
        return sizeof...(Ts) == 0;
    }

    /** Launch all runner coroutines and suspend the parent.

        Sets up stop propagation from parent to children, then launches
        each task in a runner coroutine. Returns noop_coroutine because
        runners resume the parent via signal_completion().

        CRITICAL: If the last task finishes synchronously then the parent
        coroutine resumes, destroying its frame, and destroying this object
        prior to the completion of await_suspend. Therefore, await_suspend
        must ensure `this` cannot be referenced after calling `launch_one`
        for the last time.

        @tparam Ex The executor type.
        @param continuation Handle to the parent coroutine to resume later.
        @param caller_ex Executor for dispatching child coroutines.
        @param parent_token Stop token from the parent for cancellation propagation.
        @return noop_coroutine; parent is resumed by the last completing task.
    */
    template<typename Ex>
    /**
     * @brief Prepare and start all child runners, forwarding parent cancellation and saving the parent continuation and executor.
     *
     * Stores the awaiting coroutine's continuation and caller executor into shared state, links the parent's stop token to the state's stop source (propagating an immediate stop if already requested), launches each child runner with the derived stop token and executor, and returns a noop coroutine handle so the awaiting coroutine remains suspended while children run.
     *
     * @param continuation The continuation coroutine to resume once all children complete.
     * @param caller_ex The executor used to dispatch the parent continuation and to resume child runners.
     * @param parent_token Optional stop token from the parent; if provided, its stop requests are forwarded to all children.
     * @return coro A coroutine handle equal to `std::noop_coroutine()`.
     */
    coro await_suspend(coro continuation, Ex const& caller_ex, std::stop_token parent_token = {})
    {
        state_->continuation_ = continuation;
        state_->caller_ex_ = caller_ex;

        // Forward parent's stop requests to children
        if(parent_token.stop_possible())
        {
            state_->parent_stop_callback_.emplace(
                parent_token,
                typename when_any_state<Ts...>::stop_callback_fn{&state_->stop_source_});

            if(parent_token.stop_requested())
                state_->stop_source_.request_stop();
        }

        auto token = state_->stop_source_.get_token();
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (..., launch_one<Is>(caller_ex, token));
        }(std::index_sequence_for<Ts...>{});

        return std::noop_coroutine();
    }

    /**
     * @brief Resume the awaiting coroutine once all launched runner coroutines have completed.
     *
     * This function performs no action and returns no value; results and any winner state are available
     * from the shared when_any state object maintained by the launcher.
     */
    void await_resume() const noexcept
    {
    }

private:
    /** Launch a single runner coroutine for task at index I.

        Creates the runner, configures its promise with state and executor,
        stores its handle for cleanup, and dispatches it for execution.

        @tparam I Compile-time index of the task in the tuple.
        @tparam Ex The executor type.
        @param caller_ex Executor for dispatching the runner.
        @param token Stop token for cooperative cancellation.

        @pre Ex::dispatch() and coro::resume() must not throw. If they do,
             the coroutine handle may leak.
    */
    template<std::size_t I, typename Ex>
    /**
     * @brief Create, initialize, and start the runner coroutine for task `I`.
     *
     * Initializes the runner's promise (shared state pointer, runtime index, executor, and stop token),
     * stores the coroutine handle into the shared state's runner_handles_ at position `I`, and
     * dispatches and resumes the coroutine via the provided executor.
     *
     * @tparam I Compile-time index of the task to launch.
     * @param caller_ex Executor used to dispatch and resume the runner coroutine.
     * @param token Stop token that will be forwarded to the runner's promise for cancellation.
     */
    void launch_one(Ex const& caller_ex, std::stop_token token)
    {
        auto runner = make_when_any_runner<I>(
            std::move(std::get<I>(*tasks_)), state_);

        auto h = runner.release();
        h.promise().state_ = state_;
        h.promise().index_ = I;
        h.promise().ex_ = caller_ex;
        h.promise().stop_token_ = token;

        coro ch{h};
        state_->runner_handles_[I] = ch;
        caller_ex.dispatch(ch).resume();
    }
};

} // namespace detail

/** Wait for the first task to complete.

    Races multiple heterogeneous tasks concurrently and returns when the
    first one completes. The result includes the winner's index and a
    deduplicated variant containing the result value.

    @par Example
    @code
    task<void> example() {
        auto [index, result] = co_await when_any(
            fetch_from_primary(),   // task<Response>
            fetch_from_backup()     // task<Response>
        );
        // index is 0 or 1, result holds the winner's Response
        auto response = std::get<Response>(result);
    }
    @endcode

    @tparam T0 First task's result type.
    @tparam Ts Remaining tasks' result types.
    @param task0 The first task to race.
    @param tasks Additional tasks to race concurrently.
    @return A task yielding a pair of (winner_index, result_variant).

    @par Key Features
    @li All tasks are launched concurrently
    @li Returns when first task completes (success or failure)
    @li Stop is requested for all siblings
    @li Waits for all siblings to complete before returning
    @li If winner threw, that exception is rethrown
    @li Void tasks contribute std::monostate to the variant
*/
template<typename T0, typename... Ts>
/**
 * @brief Races multiple tasks and completes with the first task that finishes.
 *
 * Launches all provided tasks concurrently and returns the index of the winning task
 * together with the winner's result stored in a deduplicated variant type.
 *
 * @param task0 First task; ownership is moved into the when_any operation.
 * @param tasks Additional tasks; each is moved into the when_any operation.
 * @returns result_type A pair where the first element is the zero-based index of the winning task and
 * the second element is a deduplicated `std::variant` holding the winner's result (void results are represented by `std::monostate`).
 * @throws Any exception thrown by the winning task, rethrown after all tasks have completed.
 */
[[nodiscard]] task<detail::when_any_result_t<T0, Ts...>>
when_any(task<T0> task0, task<Ts>... tasks)
{
    using result_type = detail::when_any_result_t<T0, Ts...>;

    detail::when_any_state<T0, Ts...> state;
    std::tuple<task<T0>, task<Ts>...> task_tuple(std::move(task0), std::move(tasks)...);

    co_await detail::when_any_launcher<T0, Ts...>(&task_tuple, &state);

    if(state.winner_exception_)
        std::rethrow_exception(state.winner_exception_);

    co_return result_type{state.winner_index_, std::move(state.result_)};
}

/** Alias for when_any result type, useful for declaring callback signatures.

    Provides a convenient public alias for the internal result type.
    The result is a pair containing the winner's index and a deduplicated
    variant holding the result value.

    @par Example
    @code
    void on_complete(when_any_result_type<int, std::string> result);
    @endcode

    @tparam T0 First task's result type.
    @tparam Ts Remaining tasks' result types.
*/
template<typename T0, typename... Ts>
using when_any_result_type = detail::when_any_result_t<T0, Ts...>;

namespace detail {

/** Shared state for homogeneous when_any (vector overload).

    Simpler than the heterogeneous version: uses std::optional<T> instead
    of variant, and std::vector instead of std::array for runner handles.

    @tparam T The common result type of all tasks.
*/
template<typename T>
struct when_any_homogeneous_state
{
    /** Counter for tasks still running.

        Completion tracking - must wait for ALL tasks for proper cleanup.
    */
    std::atomic<std::size_t> remaining_count_;

    /** Total number of tasks being raced. */
    std::size_t task_count_;

    /** Flag indicating whether a winner has been determined.

        Winner tracking - first task to complete claims this.
    */
    std::atomic<bool> has_winner_{false};

    /** Index of the winning task in the vector. */
    std::size_t winner_index_{0};

    /** Storage for the winner's result value.

        Result storage - simple value, no variant needed.
    */
    std::optional<T> result_;

    /** Exception thrown by the winner, if any. */
    std::exception_ptr winner_exception_;

    /** Handles to runner coroutines for cleanup.

        Runner handles - destroyed in destructor.
    */
    std::vector<coro> runner_handles_;

    /** Stop source for cancelling sibling tasks.

        Stop propagation - requested when winner is found.
    */
    std::stop_source stop_source_;

    /** Callback functor that forwards stop requests.

        Connects parent's stop_token to our stop_source.
    */
    struct stop_callback_fn
    {
        /** Pointer to the stop source to signal. */
        std::stop_source* source_;

        /**
 * @brief Request cancellation on the associated stop source.
 *
 * Invokes `request_stop()` on the underlying `std::stop_source`.
 */
        void operator()() const noexcept { source_->request_stop(); }
    };

    /** Type alias for the stop callback registration. */
    using stop_callback_t = std::stop_callback<stop_callback_fn>;

    /** Optional callback linking parent's stop token to our stop source. */
    std::optional<stop_callback_t> parent_stop_callback_;

    /** Parent coroutine handle to resume when all children complete. */
    coro continuation_;

    /** Executor reference for dispatching the parent resumption. */
    executor_ref caller_ex_;

    /**
     * @brief Create a shared state for racing a given number of homogeneous tasks.
     *
     * Initializes the completion counter, records the total task count,
     * and allocates storage for per-task runner coroutine handles.
     *
     * @param count Number of tasks participating in the race.
     */
    explicit when_any_homogeneous_state(std::size_t count)
        : remaining_count_(count)
        , task_count_(count)
        , runner_handles_(count)
    {
    }

    /**
     * @brief Destroy the state and release any stored runner coroutine handles.
     *
     * Iterates over stored coroutine handles in `runner_handles_` and calls `destroy()` on each non-null handle
     * to release coroutine resources and avoid leaks.
     */
    ~when_any_homogeneous_state()
    {
        for(auto h : runner_handles_)
            if(h)
                h.destroy();
    }

    /**
     * Attempt to claim winner status for the calling task.
     *
     * If successful, records the winner index and requests cancellation of sibling tasks.
     *
     * @param index The task's runtime index to record as the winner.
     * @return `true` if this task became the winner, `false` otherwise.
     */
    bool try_win(std::size_t index) noexcept
    {
        bool expected = false;
        if(has_winner_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        {
            winner_index_ = index;
            stop_source_.request_stop();
            return true;
        }
        return false;
    }

    /**
     * @brief Store the winning task's result into the state's optional storage.
     *
     * @pre Called only by the winner (i.e., after `try_win` returned `true`).
     * @param value The winning task's result; moved into the state's `std::optional<T>`.
     *
     * This operation is noexcept when moving into `std::optional<T>` is nothrow.
     */
    void set_winner_result(T value)
        noexcept(std::is_nothrow_move_assignable_v<std::optional<T>>)
    {
        result_ = std::move(value);
    }

    /**
     * Stores the winning task's exception for later inspection or rethrow.
     *
     * @param ep Exception pointer captured from the winning task.
     * @pre This is called only by the winner (i.e., after try_win returned true).
     */
    void set_winner_exception(std::exception_ptr ep) noexcept
    {
        winner_exception_ = ep;
    }

    /**
     * @brief Signals that a runner has finished and, if it was the last, resumes the parent coroutine.
     *
     * Called by each runner's final_suspend. Decrements the remaining count and, when the
     * last runner completes, dispatches the stored continuation via the caller executor so
     * the parent resumes after all child frames are destroyed.
     *
     * @return Coroutine to resume: the parent's continuation if this call observed the final completion, otherwise a noop coroutine.
     */
    coro signal_completion() noexcept
    {
        auto remaining = remaining_count_.fetch_sub(1, std::memory_order_acq_rel);
        if(remaining == 1)
            return caller_ex_.dispatch(continuation_);
        return std::noop_coroutine();
    }
};

/** Specialization for void tasks (no result storage needed).

    When racing void-returning tasks, there is no result value to store.
    Only the winner's index and any exception are tracked.
*/
template<>
struct when_any_homogeneous_state<void>
{
    /** Counter for tasks still running. */
    std::atomic<std::size_t> remaining_count_;

    /** Total number of tasks being raced. */
    std::size_t task_count_;

    /** Flag indicating whether a winner has been determined. */
    std::atomic<bool> has_winner_{false};

    /** Index of the winning task in the vector. */
    std::size_t winner_index_{0};

    /** Exception thrown by the winner, if any. */
    std::exception_ptr winner_exception_;

    /** Handles to runner coroutines for cleanup. */
    std::vector<coro> runner_handles_;

    /** Stop source for cancelling sibling tasks. */
    std::stop_source stop_source_;

    /** Callback functor that forwards stop requests. */
    struct stop_callback_fn
    {
        /** Pointer to the stop source to signal. */
        std::stop_source* source_;

        /**
 * @brief Request cancellation on the associated stop source.
 *
 * Invokes `request_stop()` on the underlying `std::stop_source`.
 */
        void operator()() const noexcept { source_->request_stop(); }
    };

    /** Type alias for the stop callback registration. */
    using stop_callback_t = std::stop_callback<stop_callback_fn>;

    /** Optional callback linking parent's stop token to our stop source. */
    std::optional<stop_callback_t> parent_stop_callback_;

    /** Parent coroutine handle to resume when all children complete. */
    coro continuation_;

    /** Executor reference for dispatching the parent resumption. */
    executor_ref caller_ex_;

    /**
     * @brief Create a shared state for racing a given number of homogeneous tasks.
     *
     * Initializes the completion counter, records the total task count,
     * and allocates storage for per-task runner coroutine handles.
     *
     * @param count Number of tasks participating in the race.
     */
    explicit when_any_homogeneous_state(std::size_t count)
        : remaining_count_(count)
        , task_count_(count)
        , runner_handles_(count)
    {
    }

    /**
     * @brief Destroy the state and release any stored runner coroutine handles.
     *
     * Iterates over stored coroutine handles in `runner_handles_` and calls `destroy()` on each non-null handle
     * to release coroutine resources and avoid leaks.
     */
    ~when_any_homogeneous_state()
    {
        for(auto h : runner_handles_)
            if(h)
                h.destroy();
    }

    /**
     * Attempt to claim winner status for the calling task.
     *
     * If successful, records the winner index and requests cancellation of sibling tasks.
     *
     * @param index The task's runtime index to record as the winner.
     * @return `true` if this task became the winner, `false` otherwise.
     */
    bool try_win(std::size_t index) noexcept
    {
        bool expected = false;
        if(has_winner_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        {
            winner_index_ = index;
            stop_source_.request_stop();
            return true;
        }
        return false;
    }

    /**
     * Stores the winning task's exception for later inspection or rethrow.
     *
     * @param ep Exception pointer captured from the winning task.
     * @pre This is called only by the winner (i.e., after try_win returned true).
     */
    void set_winner_exception(std::exception_ptr ep) noexcept
    {
        winner_exception_ = ep;
    }

    /**
     * @brief Signals that a runner has finished and, if it was the last, resumes the parent coroutine.
     *
     * Called by each runner's final_suspend. Decrements the remaining count and, when the
     * last runner completes, dispatches the stored continuation via the caller executor so
     * the parent resumes after all child frames are destroyed.
     *
     * @return Coroutine to resume: the parent's continuation if this call observed the final completion, otherwise a noop coroutine.
     */
    coro signal_completion() noexcept
    {
        auto remaining = remaining_count_.fetch_sub(1, std::memory_order_acq_rel);
        if(remaining == 1)
            return caller_ex_.dispatch(continuation_);
        return std::noop_coroutine();
    }
};

/** Wrapper coroutine for homogeneous when_any tasks (vector overload).

    Same role as when_any_runner but uses a runtime index instead of
    a compile-time index, allowing it to work with vectors of tasks.

    @tparam T The common result type of all tasks.
*/
template<typename T>
struct when_any_homogeneous_runner
{
    /** Promise type for the homogeneous runner coroutine.

        Manages executor propagation, stop token forwarding, and completion
        signaling for the wrapped child task.
    */
    struct promise_type : frame_allocating_base
    {
        /** Pointer to shared state for winner coordination. */
        when_any_homogeneous_state<T>* state_ = nullptr;

        /** Runtime index of this task in the vector. */
        std::size_t index_ = 0;

        /** Executor reference inherited from the parent coroutine. */
        executor_ref ex_;

        /** Stop token for cooperative cancellation. */
        std::stop_token stop_token_;

        /**
         * @brief Construct a runner coroutine object bound to this promise.
         *
         * @return when_any_homogeneous_runner A coroutine wrapper owning the coroutine handle created from this promise.
         */
        when_any_homogeneous_runner get_return_object()
        {
            return when_any_homogeneous_runner(
                std::coroutine_handle<promise_type>::from_promise(*this));
        }

        /**
         * @brief Suspend the coroutine immediately upon creation.
         *
         * @return An awaiter that always suspends the coroutine.
         */
        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        /**
         * @brief Final-suspend awaiter that notifies shared state this runner has finished.
         *
         * The returned awaiter always suspends; on suspend it calls the shared state's
         * signal_completion() to decrement the remaining task count and obtain the
         * coroutine to resume (the parent continuation) if this was the last runner.
         *
         * @return The parent coroutine to resume if this was the last task, `std::noop_coroutine()` otherwise.
         */
        auto final_suspend() noexcept
        {
            /** Awaiter that signals task completion and potentially resumes parent. */
            struct awaiter
            {
                /** Pointer to the promise for accessing shared state. */
                promise_type* p_;

                /**
                 * @brief Indicates the awaiter is never ready and always suspends.
                 *
                 * @return `false` — always suspend the awaiting coroutine.
                 */
                bool await_ready() const noexcept
                {
                    return false;
                }

                /**
                 * Notify the shared when_any state that this runner has completed and obtain the coroutine to resume.
                 *
                 * @return The parent coroutine to resume if this was the last outstanding task; otherwise `std::noop_coroutine()`.
                 */
                coro await_suspend(coro) noexcept
                {
                    return p_->state_->signal_completion();
                }

                /**
                 * @brief No-op await_resume for the final_suspend awaiter.
                 *
                 * Performs no action when resumed; control simply returns to the awaiting context.
                 */
                void await_resume() const noexcept
                {
                }
            };
            return awaiter{this};
        }

        /**
         * @brief Satisfies the coroutine promise's return requirement when a runner completes successfully.
         *
         * @details No-op used to indicate normal completion of the runner coroutine. Result capture and
         * propagation are performed by the runner/launcher infrastructure; this function itself performs no work.
         */
        void return_void()
        {
        }

        /**
         * @brief Handle an exception thrown by the child runner.
         *
         * Exceptions are treated as valid completions. If this runner's exception
         * becomes the winning result it is stored for rethrowing to the caller once
         * all runners complete; otherwise the exception is discarded.
         */
        void unhandled_exception()
        {
            if(state_->try_win(index_))
                state_->set_winner_exception(std::current_exception());
        }

        /** Awaiter wrapper that injects executor and stop token into child awaitables.

            @tparam Awaitable The underlying awaitable type being wrapped.
        */
        template<class Awaitable>
        struct transform_awaiter
        {
            /** The wrapped awaitable instance. */
            std::decay_t<Awaitable> a_;

            /** Pointer to promise for accessing executor and stop token. */
            promise_type* p_;

            /**
             * Indicates whether the underlying awaitable can complete synchronously.
             *
             * @return `true` if the awaitable can complete synchronously, `false` otherwise.
             */
            bool await_ready()
            {
                return a_.await_ready();
            }

            /**
             * @brief Resume the underlying awaitable and obtain its result.
             *
             * @return The value returned by the wrapped awaitable's `await_resume`.
             */
            auto await_resume()
            {
                return a_.await_resume();
            }

            /** Suspend with executor and stop token injection.

                @tparam Promise The suspending coroutine's promise type.
                @param h Handle to the suspending coroutine.
                @return Coroutine to resume or void.
            */
            template<class Promise>
            /**
             * @brief Forwards suspension to the wrapped awaitable while injecting the promise's executor and stop token.
             *
             * @param h Coroutine handle of the awaiting coroutine.
             * @return The value returned by the underlying awaitable's `await_suspend` call.
             */
            auto await_suspend(std::coroutine_handle<Promise> h)
            {
                return a_.await_suspend(h, p_->ex_, p_->stop_token_);
            }
        };

        /** Transform awaitables to inject executor and stop token.

            @tparam Awaitable The awaitable type being co_awaited.
            @param a The awaitable instance.
            @return Transformed awaiter with executor/stop_token injection.
        */
        template<class Awaitable>
        /**
         * @brief Adapts an awaitable to the runner promise's execution context.
         *
         * Produces an awaitable that will run with this promise's executor and stop token:
         * if the awaited type accepts an executor and stop token, the returned awaitable
         * forwards this promise's executor and stop token to the inner operation;
         * otherwise the returned awaitable is bound to this promise's executor.
         *
         * @tparam Awaitable Type of the awaitable being adapted.
         * @param a The awaitable to adapt.
         * @return An awaitable adapted to execute with the promise's executor and stop token.
         */
        auto await_transform(Awaitable&& a)
        {
            using A = std::decay_t<Awaitable>;
            if constexpr (IoAwaitable<A, executor_ref>)
            {
                return transform_awaiter<Awaitable>{
                    std::forward<Awaitable>(a), this};
            }
            else
            {
                return make_affine(std::forward<Awaitable>(a), ex_);
            }
        }
    };

    /** Handle to the underlying coroutine frame. */
    std::coroutine_handle<promise_type> h_;

    /**
     * @brief Create a runner wrapper that holds the given coroutine handle.
     *
     * @param h Coroutine handle for the runner's promise; stored by the wrapper.
     */
    explicit when_any_homogeneous_runner(std::coroutine_handle<promise_type> h)
        : h_(h)
    {
    }

    /** Move constructor (Clang 14 workaround).

        Clang 14 (non-Apple) has a coroutine codegen bug requiring explicit
        move constructor; other compilers work correctly with deleted move.
    */
#if defined(__clang__) && __clang_major__ == 14 && !defined(__apple_build_version__)
    /**
         * @brief Move-constructs a runner by transferring ownership of the coroutine handle.
         *
         * Transfers the internal coroutine handle from `other` into this object and leaves
         * `other` in a null (released) state.
         *
         * @param other Runner to move from; its handle is set to null after construction.
         */
        when_any_homogeneous_runner(when_any_homogeneous_runner&& other) noexcept
        : h_(std::exchange(other.h_, nullptr)) {}
#endif

    /**
 * @brief Deleted copy constructor; instances of this type cannot be copied and are move-only.
 */
    when_any_homogeneous_runner(when_any_homogeneous_runner const&) = delete;

    /** Copy assignment is not allowed. */
    when_any_homogeneous_runner& operator=(when_any_homogeneous_runner const&) = delete;

    /** Move construction is deleted (except on Clang 14). */
#if !defined(__clang__) || __clang_major__ != 14 || defined(__apple_build_version__)
    /**
 * @brief Deleted move constructor to disable moving of runner objects.
 *
 * Runner instances are not movable; ownership of the underlying coroutine handle cannot be transferred.
 */
when_any_homogeneous_runner(when_any_homogeneous_runner&&) = delete;
#endif

    /**
 * @brief Move-assignment is disabled to prevent assigning runner instances.
 *
 * The runner type is non-assignable; move assignment is explicitly deleted.
 */
    when_any_homogeneous_runner& operator=(when_any_homogeneous_runner&&) = delete;

    /** Release ownership of the coroutine handle.

        @return The coroutine handle; this object becomes empty.
    */
    auto release() noexcept
    {
        return std::exchange(h_, nullptr);
    }
};

/** Create a runner coroutine for a homogeneous when_any task.

    Factory function that creates a wrapper coroutine for a child task
    in the vector overload. Uses a runtime index instead of compile-time.

    @tparam T The result type of the task being wrapped.
    @param inner The task to run (will be moved from).
    @param state Shared state for winner coordination.
    @param index Runtime index of this task in the vector.
    @return Runner coroutine (must be started via resume()).
*/
template<typename T>
/**
 * @brief Creates a runner that awaits a single task and attempts to claim it as the winner for a homogeneous when_any.
 *
 * The returned runner awaits the provided `inner` task; upon its completion it calls `state->try_win(index)` to
 * attempt to become the race winner. If `T` is not `void` and the runner wins, the runner stores the task's result
 * into `state` via `set_winner_result`, or stores an exception via `set_winner_exception` if storing the result throws.
 *
 * @tparam T The task result type.
 * @param inner The task to be awaited by the runner.
 * @param state Pointer to the shared homogeneous when_any state used to coordinate winner selection and result storage.
 * @param index The runtime index of this task within the originating task vector; becomes the winner index if this runner wins.
 * @return when_any_homogeneous_runner<T> A coroutine runner that will execute `inner` and update `state` on completion.
 */
when_any_homogeneous_runner<T>
make_when_any_homogeneous_runner(task<T> inner, when_any_homogeneous_state<T>* state, std::size_t index)
{
    if constexpr (std::is_void_v<T>)
    {
        co_await std::move(inner);
        state->try_win(index);  // void tasks have no result to store
    }
    else
    {
        auto result = co_await std::move(inner);
        if(state->try_win(index))
        {
            try
            {
                state->set_winner_result(std::move(result));
            }
            catch(...)
            {
                state->set_winner_exception(std::current_exception());
            }
        }
    }
}

/** Awaitable that launches all runners for homogeneous when_any.

    Same lifetime concerns as when_any_launcher; see its documentation.
    Uses runtime iteration over the task vector instead of compile-time
    expansion over a tuple.

    @tparam T The common result type of all tasks in the vector.
*/
template<typename T>
class when_any_homogeneous_launcher
{
    /** Pointer to vector of tasks to launch. */
    std::vector<task<T>>* tasks_;

    /** Pointer to shared state for coordination. */
    when_any_homogeneous_state<T>* state_;

public:
    /**
     * @brief Constructs a launcher that will start runners for a vector of tasks using shared state.
     *
     * @param tasks Pointer to the vector of tasks; must be non-null and remain valid for the lifetime of the awaitable.
     * @param state Pointer to the shared when_any_homogeneous_state coordinating winner selection; must remain valid for the lifetime of the awaitable.
     */
    when_any_homogeneous_launcher(
        std::vector<task<T>>* tasks,
        when_any_homogeneous_state<T>* state)
        : tasks_(tasks)
        , state_(state)
    {
    }

    /** Check if the launcher can complete synchronously.

        @return True only if there are no tasks (degenerate case).
    */
    bool await_ready() const noexcept
    {
        return tasks_->empty();
    }

    /** Launch all runner coroutines and suspend the parent.

        Sets up stop propagation from parent to children, then launches
        each task in a runner coroutine. Returns noop_coroutine because
        runners resume the parent via signal_completion().

        CRITICAL: If the last task finishes synchronously then the parent
        coroutine resumes, destroying its frame, and destroying this object
        prior to the completion of await_suspend. Therefore, await_suspend
        must ensure `this` cannot be referenced after calling `launch_one`
        for the last time.

        @tparam Ex The executor type.
        @param continuation Handle to the parent coroutine to resume later.
        @param caller_ex Executor for dispatching child coroutines.
        @param parent_token Stop token from the parent for cancellation propagation.
        @return noop_coroutine; parent is resumed by the last completing task.
    */
    template<typename Ex>
    /**
     * @brief Stores parent continuation and executor, propagates the parent's stop requests, and launches all runner coroutines for the task vector.
     *
     * The method records the parent's continuation and executor into shared state, registers a callback to forward the provided parent stop token to the state's stop_source_ (and requests stop immediately if the parent already requested it), then creates and dispatches a runner for each task using the state's stop token.
     *
     * @param continuation Coroutine handle of the awaiting parent; saved to be resumed when all runners complete.
     * @param caller_ex Executor reference used to dispatch runners.
     * @param parent_token Parent-level stop token; if stop is possible it is linked to the state's stop_source_ and, if already requested, causes an immediate request_stop on the state's stop_source_.
     * @returns coro A noop coroutine handle (std::noop_coroutine()) to indicate the caller remains suspended while runners execute.
     */
    coro await_suspend(coro continuation, Ex const& caller_ex, std::stop_token parent_token = {})
    {
        state_->continuation_ = continuation;
        state_->caller_ex_ = caller_ex;

        // Forward parent's stop requests to children
        if(parent_token.stop_possible())
        {
            state_->parent_stop_callback_.emplace(
                parent_token,
                typename when_any_homogeneous_state<T>::stop_callback_fn{&state_->stop_source_});

            if(parent_token.stop_requested())
                state_->stop_source_.request_stop();
        }

        auto num_tasks = tasks_->size();
        auto token = state_->stop_source_.get_token();
        for(std::size_t i = 0; i < num_tasks; ++i)
            launch_one( i, caller_ex, token);

        return std::noop_coroutine();
    }

    /**
     * @brief Resume the awaiting coroutine once all launched runner coroutines have completed.
     *
     * This function performs no action and returns no value; results and any winner state are available
     * from the shared when_any state object maintained by the launcher.
     */
    void await_resume() const noexcept
    {
    }

private:
    /** Launch a single runner coroutine for task at the given index.

        Creates the runner, configures its promise with state and executor,
        stores its handle for cleanup, and dispatches it for execution.

        @tparam Ex The executor type.
        @param index Runtime index of the task in the vector.
        @param caller_ex Executor for dispatching the runner.
        @param token Stop token for cooperative cancellation.

        @pre Ex::dispatch() and coro::resume() must not throw. If they do,
             the coroutine handle may leak.
    */
    template<typename Ex>
    /**
     * @brief Starts a runner coroutine for the task at the given index and schedules it for execution.
     *
     * Initializes the runner's promise with the shared state, the runtime index, the caller executor,
     * and the provided stop token; stores the runner handle into the shared state's runner handle
     * collection and dispatches it on the caller executor.
     *
     * @param index Runtime index of the task to launch within the tasks vector.
     * @param caller_ex Executor on which the runner coroutine will be dispatched and resumed.
     * @param token Stop token used to propagate cancellation to the runner.
     */
    void launch_one(std::size_t index, Ex const& caller_ex, std::stop_token token)
    {
        auto runner = make_when_any_homogeneous_runner(
            std::move((*tasks_)[index]), state_, index);

        auto h = runner.release();
        h.promise().state_ = state_;
        h.promise().index_ = index;
        h.promise().ex_ = caller_ex;
        h.promise().stop_token_ = token;

        coro ch{h};
        state_->runner_handles_[index] = ch;
        caller_ex.dispatch(ch).resume();
    }
};

} // namespace detail

/** Wait for the first task to complete (homogeneous overload).

    Races a vector of tasks with the same result type. Simpler than the
    heterogeneous overload: returns a direct pair instead of a variant
    since all tasks share the same type.

    @par Example
    @code
    task<void> example() {
        std::vector<task<Response>> requests;
        requests.push_back(fetch_from_server(0));
        requests.push_back(fetch_from_server(1));
        requests.push_back(fetch_from_server(2));

        auto [index, response] = co_await when_any(std::move(requests));
        // index is 0, 1, or 2; response is the winner's Response
    }
    @endcode

    @tparam T The common result type of all tasks (must not be void).
    @param tasks Vector of tasks to race concurrently (must not be empty).
    @return A task yielding a pair of (winner_index, result).
    @throws std::invalid_argument if tasks is empty.

    @par Key Features
    @li All tasks are launched concurrently
    @li Returns when first task completes (success or failure)
    @li Stop is requested for all siblings
    @li Waits for all siblings to complete before returning
    @li If winner threw, that exception is rethrown
    @li Returns simple pair (no variant needed for homogeneous types)
*/
template<typename T>
    requires (!std::is_void_v<T>)
/**
 * @brief Waits for the first-completing task in a homogeneous vector and returns its index and value.
 *
 * Awaits all provided tasks concurrently, cancels remaining tasks when a winner is selected, and returns
 * a pair containing the winner's zero-based index and the winner's result value.
 *
 * @param tasks Vector of tasks to race; each task must yield a value of type `T`.
 * @return std::pair<std::size_t, T> Pair where the first element is the winner's index and the second is the winner's result (moved).
 * @throws std::invalid_argument if `tasks` is empty.
 * @throws Any exception propagated from the winning task (rethrown after all tasks complete).
 */
[[nodiscard]] task<std::pair<std::size_t, T>>
when_any(std::vector<task<T>> tasks)
{
    if(tasks.empty())
        throw std::invalid_argument("when_any requires at least one task");

    using result_type = std::pair<std::size_t, T>;

    detail::when_any_homogeneous_state<T> state(tasks.size());

    co_await detail::when_any_homogeneous_launcher<T>(&tasks, &state);

    if(state.winner_exception_)
        std::rethrow_exception(state.winner_exception_);

    co_return result_type{state.winner_index_, std::move(*state.result_)};
}

/**
 * @brief Race a set of void-returning tasks and produce the zero-based index of the first task to complete.
 *
 * @param tasks Vector of void tasks to race; must contain at least one task.
 * @return Zero-based index of the task that completed first.
 * @throws std::invalid_argument if `tasks` is empty.
 *
 * If the winning task throws an exception, that exception is rethrown after all tasks have finished.
 */
[[nodiscard]] inline task<std::size_t>
when_any(std::vector<task<void>> tasks)
{
    if(tasks.empty())
        throw std::invalid_argument("when_any requires at least one task");

    detail::when_any_homogeneous_state<void> state(tasks.size());

    co_await detail::when_any_homogeneous_launcher<void>(&tasks, &state);

    if(state.winner_exception_)
        std::rethrow_exception(state.winner_exception_);

    co_return state.winner_index_;
}

/** Alias for vector when_any result type.

    For homogeneous when_any (vector overload), the result is simpler:
    a pair of the winner's index and the result value directly (no variant
    needed since all tasks share the same type).

    @par Example
    @code
    void on_complete(when_any_vector_result_type<Response> result);
    @endcode

    @tparam T The common result type of all tasks in the vector.
*/
template<typename T>
using when_any_vector_result_type = std::pair<std::size_t, T>;

} // namespace capy
} // namespace boost

#endif