//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_MAKE_AFFINE_HPP
#define BOOST_CAPY_MAKE_AFFINE_HPP

#include <boost/capy/detail/config.hpp>

#ifdef BOOST_CAPY_HAS_CORO

#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace boost {
namespace capy {

namespace detail {

template<typename Dispatcher>
struct dispatch_awaitable
{
    Dispatcher& dispatcher_;

    bool
    await_ready() const noexcept
    {
        return false;
    }

    void
    await_suspend(std::coroutine_handle<> h) const
    {
        dispatcher_(
            [h]() mutable
            {
                h.resume();
            });
    }

    void
    await_resume() const noexcept
    {
    }
};

template<typename T>
auto
get_awaitable(T&& expr)
{
    if constexpr(requires { std::forward<T>(expr).operator co_await(); })
        return std::forward<T>(expr).operator co_await();
    else if constexpr(requires { operator co_await(std::forward<T>(expr)); })
        return operator co_await(std::forward<T>(expr));
    else
        return std::forward<T>(expr);
}

template<typename T>
using awaitable_type = decltype(get_awaitable(std::declval<T>()));

template<typename A>
using await_result_t = decltype(std::declval<awaitable_type<A>>().await_resume());

struct transfer_to_caller
{
    std::coroutine_handle<> caller_;

    bool
    await_ready() noexcept
    {
        return false;
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<>) noexcept
    {
        return caller_;
    }

    void
    await_resume() noexcept
    {
    }
};

template<typename T>
class affinity_trampoline
{
public:
    struct promise_type
    {
        std::optional<T> value_;
        std::exception_ptr exception_;
        std::coroutine_handle<> caller_;

        affinity_trampoline
        get_return_object()
        {
            return affinity_trampoline{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always
        initial_suspend() noexcept
        {
            return {};
        }

        transfer_to_caller
        final_suspend() noexcept
        {
            return {caller_};
        }

        template<typename U>
        void
        return_value(U&& v)
        {
            value_.emplace(std::forward<U>(v));
        }

        void
        unhandled_exception()
        {
            exception_ = std::current_exception();
        }
    };

private:
    std::coroutine_handle<promise_type> handle_;

public:
    explicit
    affinity_trampoline(std::coroutine_handle<promise_type> h)
        : handle_(h)
    {
    }

    affinity_trampoline(affinity_trampoline&& o) noexcept
        : handle_(std::exchange(o.handle_, {}))
    {
    }

    ~affinity_trampoline()
    {
        if(handle_)
            handle_.destroy();
    }

    bool
    await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> caller) noexcept
    {
        handle_.promise().caller_ = caller;
        return handle_;
    }

    T
    await_resume()
    {
        if(handle_.promise().exception_)
            std::rethrow_exception(handle_.promise().exception_);
        return std::move(*handle_.promise().value_);
    }
};

template<>
class affinity_trampoline<void>
{
public:
    struct promise_type
    {
        std::exception_ptr exception_;
        std::coroutine_handle<> caller_;

        affinity_trampoline
        get_return_object()
        {
            return affinity_trampoline{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always
        initial_suspend() noexcept
        {
            return {};
        }

        transfer_to_caller
        final_suspend() noexcept
        {
            return {caller_};
        }

        void
        return_void() noexcept
        {
        }

        void
        unhandled_exception()
        {
            exception_ = std::current_exception();
        }
    };

private:
    std::coroutine_handle<promise_type> handle_;

public:
    explicit
    affinity_trampoline(std::coroutine_handle<promise_type> h)
        : handle_(h)
    {
    }

    affinity_trampoline(affinity_trampoline&& o) noexcept
        : handle_(std::exchange(o.handle_, {}))
    {
    }

    ~affinity_trampoline()
    {
        if(handle_)
            handle_.destroy();
    }

    bool
    await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> caller) noexcept
    {
        handle_.promise().caller_ = caller;
        return handle_;
    }

    void
    await_resume()
    {
        if(handle_.promise().exception_)
            std::rethrow_exception(handle_.promise().exception_);
    }
};

} // detail

//------------------------------------------------

/** Create an affinity trampoline for an awaitable.

    This function wraps an awaitable in a trampoline coroutine
    that ensures resumption occurs via the specified dispatcher.
    After the inner awaitable completes, the trampoline dispatches
    the continuation to the dispatcher before transferring control
    back to the caller.

    When used with `await_transform`, this enables executor affinity
    for coroutines - ensuring that after any `co_await`, the coroutine
    resumes on its designated executor regardless of where the
    awaited operation completed.

    @par Example
    @code
    struct my_task
    {
        struct promise_type
        {
            Dispatcher* dispatcher_;

            template<typename Awaitable>
            auto
            await_transform(Awaitable&& a)
            {
                return make_affine(
                    std::forward<Awaitable>(a),
                    *dispatcher_);
            }

            // ... other promise_type members
        };

        // ... other task members
    };
    @endcode

    @par HALO Optimization
    The trampoline coroutine is designed to be elided by the
    compiler's Heap Allocation eLision Optimization (HALO),
    resulting in zero allocation overhead.

    @par Dispatcher
    The dispatcher is a unary function object that accepts a
    nullary function object and arranges for it to be invoked.
    The equivalent signature is:
    @code
    struct Dispatcher
    {
        template<typename F>
        void operator()(F&& f);
    };
    @endcode

    @param awaitable The awaitable to wrap.
    @param dispatcher A unary function used to dispatch the continuation.

    @return An awaitable trampoline that yields the same result
        as the wrapped awaitable.
*/
template<
    typename Awaitable,
    typename Dispatcher>
auto
make_affine(
    Awaitable&& awaitable,
    Dispatcher& dispatcher) ->
        detail::affinity_trampoline<
            detail::await_result_t<Awaitable>>
{
    using result_t = detail::await_result_t<Awaitable>;

    if constexpr(std::is_void_v<result_t>)
    {
        co_await detail::get_awaitable(std::forward<Awaitable>(awaitable));
        co_await detail::dispatch_awaitable<Dispatcher>{dispatcher};
    }
    else
    {
        auto result = co_await detail::get_awaitable(std::forward<Awaitable>(awaitable));
        co_await detail::dispatch_awaitable<Dispatcher>{dispatcher};
        co_return result;
    }
}

} // capy
} // boost

#endif

#endif

