//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef EARLY_QUITTER_HPP
#define EARLY_QUITTER_HPP

/* A quitter built only on capy's public coroutine protocol.

   A stop ends a quitter at the co_await it is suspended in, without
   resuming its body and without throwing. The quitter that sees the
   stop walks up its run of quitters to the topmost one, stores the
   stopped value (canceled) as that quitter's result, and resumes
   whatever awaited it: a task, run, run_async, or a combinator. To
   that awaiter the quitter simply completed. Destroying the top
   quitter then destroys the suspended frames below it, so RAII runs
   at every level.

   Unlike a task, whose locals are gone by the time it completes, a
   stopped quitter's locals live until its owner destroys the frame.
   run_async calls its handler first, so a handler that signals "done"
   runs before the quitter's RAII cleanup: wait for the executor's
   work to finish (thread_pool::join, for example) before touching
   state those destructors use.

   Each quitter hands its operations a gate (a small coroutine frame)
   instead of its own handle. A completion resumes the gate, which
   decides before the body runs: with a stop pending, the operation's
   result is taken early, and a canceled results or error_code (or any
   result that cannot carry the stop) ends the quitter; any other
   result is delivered, and the stop lands at the next co_await.

   Because the stop is a value, the first awaiter that is not a
   quitter must receive a T of results<...> or std::error_code.
*/

#include <boost/capy/concept/context_awaitable.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/context_awaitable_promise_base.hpp>
#include <boost/capy/ex/frame_alloc_mixin.hpp>
#include <boost/capy/ex/frame_allocator.hpp>

#include <coroutine>
#include <exception>
#include <optional>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace early {

namespace capy = boost::capy;

/// Test whether `T` can carry a stop as a value.
template<class T>
struct carries_stop : std::false_type {};

template<>
struct carries_stop<std::error_code> : std::true_type {};

template<class... Ts>
struct carries_stop<std::tuple<std::error_code, Ts...>> : std::true_type {};

template<class T>
inline constexpr bool carries_stop_v = carries_stop<T>::value;

template<class T>
class quitter;

namespace detail {

template<class T>
struct is_quitter : std::false_type {};

template<class T>
struct is_quitter<quitter<T>> : std::true_type {};

template<class T>
bool is_canceled(T const& v) noexcept
{
    if constexpr (std::is_same_v<T, std::error_code>)
        return v == capy::cond::canceled;
    else
        return std::get<0>(v) == capy::cond::canceled;
}

template<class T>
T stopped_value()
{
    std::error_code ec = capy::error::canceled;
    if constexpr (std::is_same_v<T, std::error_code>)
        return ec;
    else
        return [&]<class... Ts>(std::tuple<std::error_code, Ts...>*) {
            return T{ec, Ts{}...};
        }(static_cast<T*>(nullptr));
}

class promise_base;

// What a quitter's operations resume instead of its body.
struct gate
{
    struct promise_type : capy::frame_alloc_mixin
    {
        promise_base* owner = nullptr;

        gate get_return_object() noexcept
        {
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };

    std::coroutine_handle<promise_type> h;
};

// The parts of a quitter's promise that do not depend on T.
class promise_base
{
protected:
    ~promise_base()
    {
        if(gate_)
            gate_.destroy();
    }

public:
    promise_base* parent_ = nullptr;
    std::coroutine_handle<> body_;
    void* awaiter_ = nullptr;
    bool (*stops_)(void*) noexcept = nullptr;
    bool stopped_ = false;
    capy::await_context const* stop_env_ = nullptr;

    virtual void store_stopped() noexcept = 0;
    virtual std::coroutine_handle<> take_continuation() noexcept = 0;

    bool stop_pending() const noexcept
    {
        return stop_env_->stop_token.stop_requested();
    }

    std::coroutine_handle<> gate_handle();

    // End the run of quitters this one belongs to, delivering the
    // stopped value from the top.
    std::coroutine_handle<> stop() noexcept
    {
        promise_base* top = this;
        top->stopped_ = true;
        while(top->parent_)
        {
            top = top->parent_;
            top->stopped_ = true;
        }
        top->store_stopped();
        return top->take_continuation();
    }

    std::coroutine_handle<> decide() noexcept
    {
        if(stop_pending() && stops_(awaiter_))
            return stop();
        return body_;
    }

private:
    std::coroutine_handle<gate::promise_type> gate_;
};

struct decide_awaiter
{
    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<gate::promise_type> h) const noexcept
    {
        return h.promise().owner->decide();
    }

    void await_resume() const noexcept {}
};

inline gate make_gate()
{
    for(;;)
        co_await decide_awaiter{};
}

inline std::coroutine_handle<> promise_base::gate_handle()
{
    if(!gate_)
    {
        gate_ = make_gate().h;
        gate_.promise().owner = this;
    }
    return gate_;
}

template<class T>
struct return_base
{
    std::optional<T> result_;

    void return_value(T v) { result_.emplace(std::move(v)); }
    T&& result() noexcept { return std::move(*result_); }
};

template<>
struct return_base<void>
{
    void return_void() noexcept {}
};

} // namespace detail

/** A coroutine that a stop ends at its current `co_await`.

    The body needs no cancellation checks: with a stop requested, the
    quitter ends at the next `co_await` that cannot complete without
    it, and its frames are destroyed without resuming. Awaited by
    another quitter, it ends that one too. Awaited by anything else,
    it completes with `canceled`, so `T` must then be `results<...>`
    or `std::error_code`.

    A stopped quitter's locals are destroyed when its owner destroys
    it, not when it completes. Under `run_async` that is after the
    handler returns.

    @tparam T The result type.
*/
template<class T = void>
class [[nodiscard]] quitter
{
public:
    struct promise_type
        : capy::context_awaitable_promise_base<promise_type>
        , detail::promise_base
        , detail::return_base<T>
    {
        using base = capy::context_awaitable_promise_base<promise_type>;

        std::exception_ptr ep_;

        quitter get_return_object() noexcept
        {
            return quitter{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto initial_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() const noexcept { return false; }
                void await_suspend(std::coroutine_handle<>) const noexcept {}
                void await_resume() const noexcept
                {
                    capy::set_current_frame_allocator(
                        p_->environment()->frame_allocator);
                }
            };
            return awaiter{this};
        }

        auto final_suspend() noexcept
        {
            struct awaiter
            {
                promise_type* p_;
                bool await_ready() const noexcept { return false; }
                std::coroutine_handle<> await_suspend(
                    std::coroutine_handle<>) const noexcept
                {
                    return p_->base::continuation();
                }
                void await_resume() const noexcept {}
            };
            return awaiter{this};
        }

        void unhandled_exception() noexcept
        {
            ep_ = std::current_exception();
        }

        std::exception_ptr exception() const noexcept { return ep_; }

        // Only a launcher calls this, and it reads the result, which
        // must be able to hold the stop.
        void set_continuation(std::coroutine_handle<> h) noexcept
        {
            static_assert(carries_stop_v<T>,
                "a launched quitter must have T = results<...> or "
                "std::error_code");
            base::set_continuation(h);
        }

        // Also kept in the base, so the stop check needs no virtual call.
        void set_environment(capy::await_context const* env) noexcept
        {
            base::set_environment(env);
            this->stop_env_ = env;
        }

        void store_stopped() noexcept override
        {
            if constexpr (carries_stop_v<T>)
                this->result_.emplace(detail::stopped_value<T>());
            else
                std::terminate(); // the top of a run always carries the stop
        }

        std::coroutine_handle<> take_continuation() noexcept override
        {
            return base::continuation();
        }

        template<class Awaitable>
        struct transform_awaiter
        {
            using A = std::decay_t<Awaitable>;
            using R = decltype(std::declval<A&>().await_resume());

            // Hold a stop-carrying result taken early by value; any
            // other result cannot carry the stop, so a stop wins.
            static constexpr bool holds =
                !std::is_reference_v<R> && carries_stop_v<R>;
            using held_type = std::conditional_t<holds, R, char>;

            A a_;
            promise_type* p_;
            std::optional<held_type> held_{};
            std::exception_ptr ep_{};
            bool stop_here_ = false;

            bool stops_on_completion() noexcept
            {
                try
                {
                    if constexpr (holds)
                    {
                        held_.emplace(a_.await_resume());
                        return detail::is_canceled(*held_);
                    }
                    else
                    {
                        // Called so anything handed over is destroyed.
                        static_cast<void>(a_.await_resume());
                        return true;
                    }
                }
                catch(...)
                {
                    ep_ = std::current_exception();
                    return false;
                }
            }

            static bool stops_at_gate(void* self) noexcept
            {
                return static_cast<transform_awaiter*>(
                    self)->stops_on_completion();
            }

            bool await_ready()
            {
                if(p_->stop_pending())
                {
                    stop_here_ = true;
                    return false;
                }
                if(!a_.await_ready())
                    return false;
                stop_here_ = p_->stop_pending() && stops_on_completion();
                return !stop_here_;
            }

            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> h)
            {
                if(stop_here_)
                    return p_->stop();
                p_->body_ = h;
                p_->awaiter_ = this;
                p_->stops_ = &stops_at_gate;
                auto gate = p_->gate_handle();
                auto const* env = p_->environment();
                if constexpr (detail::is_quitter<A>::value)
                {
                    return a_.start(gate, env, p_);
                }
                else
                {
                    using S = decltype(a_.await_suspend(gate, env));
                    if constexpr (std::is_void_v<S>)
                    {
                        a_.await_suspend(gate, env);
                        return std::noop_coroutine();
                    }
                    else if constexpr (std::is_same_v<S, bool>)
                    {
                        if(a_.await_suspend(gate, env))
                            return std::noop_coroutine();
                        return p_->decide();
                    }
                    else
                    {
                        return a_.await_suspend(gate, env);
                    }
                }
            }

            decltype(auto) await_resume()
            {
                capy::set_current_frame_allocator(
                    p_->environment()->frame_allocator);
                if(ep_)
                    std::rethrow_exception(std::exchange(ep_, nullptr));
                if constexpr (holds)
                {
                    if(held_)
                        return R(std::move(*held_));
                }
                return a_.await_resume();
            }
        };

        template<class Awaitable>
        auto transform_awaitable(Awaitable&& a)
        {
            static_assert(capy::ContextAwaitable<std::decay_t<Awaitable>>,
                "requires ContextAwaitable");
            return transform_awaiter<Awaitable>{
                std::forward<Awaitable>(a), this};
        }
    };

    /// Destroy the quitter and its frame.
    ~quitter() { destroy_frame(); }

    /// Construct by taking the frame of `other`.
    quitter(quitter&& other) noexcept
        : h_(std::exchange(other.h_, nullptr))
        , lent_(std::exchange(other.lent_, false))
    {
    }

    /// Destroy the held frame, then take the frame of `other`.
    quitter& operator=(quitter&& other) noexcept
    {
        if(this != &other)
        {
            destroy_frame();
            h_ = std::exchange(other.h_, nullptr);
            lent_ = std::exchange(other.lent_, false);
        }
        return *this;
    }

    /// Return false: a quitter always starts by suspending its awaiter.
    bool await_ready() const noexcept { return false; }

    /** Start the quitter, awaited by something that is not a quitter.

        @param cont The awaiter, resumed when the quitter completes or
            stops.
        @param env The environment the quitter runs in.

        @return The quitter's frame, to resume.
    */
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> cont, capy::await_context const* env)
    {
        static_assert(carries_stop_v<T>,
            "a quitter awaited outside a quitter must have "
            "T = results<...> or std::error_code");
        return start(cont, env, nullptr);
    }

    /** Return the quitter's result, or its stopped value.

        @throws Any exception the body let escape.
    */
    auto await_resume()
    {
        lent_ = false;
        auto& p = h_.promise();
        if(p.ep_)
            std::rethrow_exception(p.ep_);
        if constexpr (!std::is_void_v<T>)
            return std::move(*p.result_);
    }

    /// Return the coroutine handle; the quitter keeps the frame.
    std::coroutine_handle<promise_type> handle() const noexcept { return h_; }

    /// Give up the frame without destroying it.
    void release() noexcept { h_ = nullptr; }

private:
    template<class>
    friend class quitter;

    std::coroutine_handle<promise_type> h_;
    bool lent_ = false;

    explicit quitter(std::coroutine_handle<promise_type> h) noexcept
        : h_(h)
    {
    }

    std::coroutine_handle<> start(
        std::coroutine_handle<> cont,
        capy::await_context const* env,
        detail::promise_base* parent) noexcept
    {
        auto& p = h_.promise();
        p.parent_ = parent;
        p.promise_type::base::set_continuation(cont);
        p.set_environment(env);
        lent_ = true;
        return h_;
    }

    // A lent frame is destroyed only by a stop of its run, or by the
    // unwind of an abandoned chain.
    void destroy_frame() noexcept
    {
        if(!h_)
            return;
        if(lent_ && !h_.done())
        {
            auto& p = h_.promise();
            auto* owner = p.environment()->owner;
            if(!p.stopped_ && !(owner && owner->abandoned()))
                std::terminate();
        }
        std::exchange(h_, nullptr).destroy();
        lent_ = false;
    }
};

} // namespace early

#endif
