//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/buffer_source.hpp>

#include <boost/capy/ex/io_env.hpp>

#include <system_error>

#include <cstddef>
#include <span>
#include <utility>

namespace boost {
namespace capy {

namespace {

// Mock IoAwaitable returning (error_code, span<const_buffer>)
struct mock_source_awaitable
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        std::coroutine_handle<>,
        io_env const*) const noexcept
    {
    }

    std::pair<std::error_code, std::span<const_buffer>>
    await_resume() const noexcept
    {
        return {};
    }
};

// Mock IoAwaitable returning wrong type (just error_code)
struct mock_source_awaitable_wrong_type
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        std::coroutine_handle<>,
        io_env const*) const noexcept
    {
    }

    std::error_code
    await_resume() const noexcept
    {
        return {};
    }
};

// Mock awaitable missing IoAwaitable protocol
struct mock_source_awaitable_not_io
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(std::coroutine_handle<>) const noexcept {}

    std::pair<std::error_code, std::size_t>
    await_resume() const noexcept
    {
        return {};
    }
};

//----------------------------------------------------------
// Mock source types
//----------------------------------------------------------

// Valid BufferSource
struct valid_buffer_source
{
    mock_source_awaitable
    pull(std::span<const_buffer>)
    {
        return {};
    }

    void consume(std::size_t) noexcept {}
};

// Invalid: pull returns wrong type
struct invalid_buffer_source_wrong_type
{
    mock_source_awaitable_wrong_type
    pull(std::span<const_buffer>)
    {
        return {};
    }
};

// Invalid: missing pull
struct invalid_buffer_source_no_pull
{
    // No pull method
};

// Invalid: pull is not IoAwaitable
struct invalid_buffer_source_not_io
{
    mock_source_awaitable_not_io
    pull(std::span<const_buffer>)
    {
        return {};
    }
};

// Invalid: pull returns non-awaitable
struct invalid_buffer_source_returns_int
{
    int pull(std::span<const_buffer>) { return 0; }
};

// Invalid: pull has wrong signature (old style with ptr+count)
struct invalid_buffer_source_wrong_sig
{
    mock_source_awaitable
    pull(const_buffer*, std::size_t) // Old signature
    {
        return {};
    }

    void consume(std::size_t) noexcept {}
};

// Invalid: missing consume
struct invalid_buffer_source_no_consume
{
    mock_source_awaitable
    pull(std::span<const_buffer>)
    {
        return {};
    }
};

} // namespace

//----------------------------------------------------------
// Static assertions
//----------------------------------------------------------

// Valid sources satisfy BufferSource
static_assert(BufferSource<valid_buffer_source>);

// Wrong return types do not satisfy BufferSource
static_assert(!BufferSource<invalid_buffer_source_wrong_type>);

// Missing methods do not satisfy BufferSource
static_assert(!BufferSource<invalid_buffer_source_no_pull>);

// Non-IoAwaitable does not satisfy BufferSource
static_assert(!BufferSource<invalid_buffer_source_not_io>);

// Non-awaitable return does not satisfy BufferSource
static_assert(!BufferSource<invalid_buffer_source_returns_int>);

// Wrong signature does not satisfy BufferSource
static_assert(!BufferSource<invalid_buffer_source_wrong_sig>);

// Missing consume does not satisfy BufferSource
static_assert(!BufferSource<invalid_buffer_source_no_consume>);

} // namespace capy
} // namespace boost
