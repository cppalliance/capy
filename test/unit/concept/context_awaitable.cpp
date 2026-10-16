//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/context_awaitable.hpp>

#include <coroutine>
#include <cstddef>
#include <vector>

namespace boost {
namespace capy {

namespace {

struct mock_promise
{
};

// Valid awaitable for each await_suspend return type

struct awaitable_suspend_void
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        std::coroutine_handle<>,
        await_context const*) const noexcept
    {
    }

    void await_resume() const noexcept {}
};

struct awaitable_suspend_bool
{
    bool await_ready() const noexcept { return true; }

    bool await_suspend(
        std::coroutine_handle<>,
        await_context const*) const noexcept
    {
        return false;
    }

    int await_resume() const noexcept { return 0; }
};

struct awaitable_suspend_handle
{
    bool await_ready() const noexcept { return true; }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h,
        await_context const*) const noexcept
    {
        return h;
    }

    void await_resume() const noexcept {}
};

struct awaitable_suspend_typed_handle
{
    bool await_ready() const noexcept { return true; }

    std::coroutine_handle<mock_promise> await_suspend(
        std::coroutine_handle<>,
        await_context const*) const noexcept
    {
        return {};
    }

    void await_resume() const noexcept {}
};

// Invalid: await_suspend returns a type the language rejects
struct awaitable_suspend_int
{
    bool await_ready() const noexcept { return true; }

    int await_suspend(
        std::coroutine_handle<>,
        await_context const*) const noexcept
    {
        return 0;
    }

    void await_resume() const noexcept {}
};

// Invalid: standard one-argument await_suspend only
struct awaitable_one_arg_suspend
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(std::coroutine_handle<>) const noexcept {}

    void await_resume() const noexcept {}
};

// Invalid: await_ready does not return bool
struct awaitable_ready_void
{
    void await_ready() const noexcept {}

    void await_suspend(
        std::coroutine_handle<>,
        await_context const*) const noexcept
    {
    }

    void await_resume() const noexcept {}
};

// Invalid: not move constructible
struct awaitable_no_move
{
    awaitable_no_move(awaitable_no_move&&) = delete;

    bool await_ready() const noexcept { return true; }

    void await_suspend(
        std::coroutine_handle<>,
        await_context const*) const noexcept
    {
    }

    void await_resume() const noexcept {}
};

// Convertible to a handle but not a handle
struct handle_convertible
{
    operator std::coroutine_handle<>() const noexcept { return {}; }
};

} // namespace

// AwaitSuspendResult accepts exactly void, bool, and coroutine handles
static_assert(AwaitSuspendResult<void>);
static_assert(AwaitSuspendResult<bool>);
static_assert(AwaitSuspendResult<std::coroutine_handle<>>);
static_assert(AwaitSuspendResult<std::coroutine_handle<mock_promise>>);
static_assert(AwaitSuspendResult<decltype(std::noop_coroutine())>);

static_assert(!AwaitSuspendResult<int>);
static_assert(!AwaitSuspendResult<std::nullptr_t>);
static_assert(!AwaitSuspendResult<handle_convertible>);
static_assert(!AwaitSuspendResult<std::coroutine_handle<>*>);

// Every valid await_suspend return type satisfies ContextAwaitable
static_assert(ContextAwaitable<awaitable_suspend_void>);
static_assert(ContextAwaitable<awaitable_suspend_bool>);
static_assert(ContextAwaitable<awaitable_suspend_handle>);
static_assert(ContextAwaitable<awaitable_suspend_typed_handle>);

// Invalid protocol shapes are rejected
static_assert(!ContextAwaitable<awaitable_suspend_int>);
static_assert(!ContextAwaitable<awaitable_one_arg_suspend>);
static_assert(!ContextAwaitable<awaitable_ready_void>);
static_assert(!ContextAwaitable<awaitable_no_move>);
static_assert(!ContextAwaitable<int>);

// awaitable_result_t names the await_resume return type
static_assert(std::same_as<awaitable_result_t<awaitable_suspend_void>, void>);
static_assert(std::same_as<awaitable_result_t<awaitable_suspend_bool>, int>);

// ContextAwaitableRange requires a sized input range of ContextAwaitables
static_assert(ContextAwaitableRange<std::vector<awaitable_suspend_void>>);
static_assert(!ContextAwaitableRange<std::vector<awaitable_one_arg_suspend>>);
static_assert(!ContextAwaitableRange<awaitable_suspend_void>);

} // namespace capy
} // namespace boost
