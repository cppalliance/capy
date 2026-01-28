//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/buffer_sink.hpp>

#include <system_error>

#include <cstddef>
#include <stop_token>
#include <utility>

namespace boost {
namespace capy {

namespace {

// Mock IoAwaitable returning (error_code)
struct mock_commit_awaitable
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        executor_ref,
        std::stop_token) const noexcept
    {
    }

    std::tuple<std::error_code>
    await_resume() const noexcept
    {
        return {};
    }
};

// Mock IoAwaitable returning wrong type (error_code, size_t)
struct mock_commit_awaitable_wrong_type
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        executor_ref,
        std::stop_token) const noexcept
    {
    }

    std::pair<std::error_code, std::size_t>
    await_resume() const noexcept
    {
        return {};
    }
};

// Mock awaitable missing IoAwaitable protocol
struct mock_commit_awaitable_not_io
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(std::coroutine_handle<>) const noexcept {}

    std::tuple<std::error_code>
    await_resume() const noexcept
    {
        return {};
    }
};

//----------------------------------------------------------
// Mock sink types
//----------------------------------------------------------

// Valid BufferSink
struct valid_buffer_sink
{
    std::size_t
    prepare(mutable_buffer*, std::size_t)
    {
        return 0;
    }

    mock_commit_awaitable
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable
    commit(std::size_t, bool)
    {
        return {};
    }

    mock_commit_awaitable
    commit_eof()
    {
        return {};
    }
};

// Invalid: commit returns wrong type
struct invalid_buffer_sink_wrong_type
{
    std::size_t
    prepare(mutable_buffer*, std::size_t)
    {
        return 0;
    }

    mock_commit_awaitable_wrong_type
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable_wrong_type
    commit(std::size_t, bool)
    {
        return {};
    }

    mock_commit_awaitable_wrong_type
    commit_eof()
    {
        return {};
    }
};

// Invalid: missing prepare
struct invalid_buffer_sink_no_prepare
{
    mock_commit_awaitable
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable
    commit(std::size_t, bool)
    {
        return {};
    }

    mock_commit_awaitable
    commit_eof()
    {
        return {};
    }
};

// Invalid: missing commit
struct invalid_buffer_sink_no_commit
{
    std::size_t
    prepare(mutable_buffer*, std::size_t)
    {
        return 0;
    }

    mock_commit_awaitable
    commit_eof()
    {
        return {};
    }
};

// Invalid: missing commit_eof
struct invalid_buffer_sink_no_commit_eof
{
    std::size_t
    prepare(mutable_buffer*, std::size_t)
    {
        return 0;
    }

    mock_commit_awaitable
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable
    commit(std::size_t, bool)
    {
        return {};
    }
};

// Invalid: commit is not IoAwaitable
struct invalid_buffer_sink_not_io
{
    std::size_t
    prepare(mutable_buffer*, std::size_t)
    {
        return 0;
    }

    mock_commit_awaitable_not_io
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable_not_io
    commit(std::size_t, bool)
    {
        return {};
    }

    mock_commit_awaitable_not_io
    commit_eof()
    {
        return {};
    }
};

// Invalid: prepare returns non-size_t
struct invalid_buffer_sink_prepare_returns_void
{
    void
    prepare(mutable_buffer*, std::size_t)
    {
    }

    mock_commit_awaitable
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable
    commit(std::size_t, bool)
    {
        return {};
    }

    mock_commit_awaitable
    commit_eof()
    {
        return {};
    }
};

// Invalid: prepare has wrong signature
struct invalid_buffer_sink_wrong_sig
{
    std::size_t
    prepare(mutable_buffer*) // Missing max_count
    {
        return 0;
    }

    mock_commit_awaitable
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable
    commit(std::size_t, bool)
    {
        return {};
    }

    mock_commit_awaitable
    commit_eof()
    {
        return {};
    }
};

} // namespace

//----------------------------------------------------------
// Static assertions
//----------------------------------------------------------

// Valid sinks satisfy BufferSink
static_assert(BufferSink<valid_buffer_sink>);

// Wrong return types do not satisfy BufferSink
static_assert(!BufferSink<invalid_buffer_sink_wrong_type>);

// Missing methods do not satisfy BufferSink
static_assert(!BufferSink<invalid_buffer_sink_no_prepare>);
static_assert(!BufferSink<invalid_buffer_sink_no_commit>);
static_assert(!BufferSink<invalid_buffer_sink_no_commit_eof>);

// Non-IoAwaitable does not satisfy BufferSink
static_assert(!BufferSink<invalid_buffer_sink_not_io>);

// Wrong prepare return type does not satisfy BufferSink
static_assert(!BufferSink<invalid_buffer_sink_prepare_returns_void>);

// Wrong signature does not satisfy BufferSink
static_assert(!BufferSink<invalid_buffer_sink_wrong_sig>);

} // namespace capy
} // namespace boost
