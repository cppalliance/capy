//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/buffer_sink.hpp>

#include <boost/capy/ex/io_env.hpp>

#include <system_error>

#include <cstddef>
#include <span>
#include <utility>

namespace boost {
namespace capy {

namespace {

// Mock IoAwaitable returning (error_code)
struct mock_commit_awaitable
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        std::coroutine_handle<>,
        io_env const&) const noexcept
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
        std::coroutine_handle<>,
        io_env const&) const noexcept
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
    std::span<mutable_buffer>
    prepare(std::span<mutable_buffer>)
    {
        return {};
    }

    mock_commit_awaitable
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable
    commit_eof(std::size_t)
    {
        return {};
    }
};

// Invalid: commit returns wrong type
struct invalid_buffer_sink_wrong_type
{
    std::span<mutable_buffer>
    prepare(std::span<mutable_buffer>)
    {
        return {};
    }

    mock_commit_awaitable_wrong_type
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable_wrong_type
    commit_eof(std::size_t)
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
    commit_eof(std::size_t)
    {
        return {};
    }
};

// Invalid: missing commit
struct invalid_buffer_sink_no_commit
{
    std::span<mutable_buffer>
    prepare(std::span<mutable_buffer>)
    {
        return {};
    }

    mock_commit_awaitable
    commit_eof(std::size_t)
    {
        return {};
    }
};

// Invalid: missing commit_eof
struct invalid_buffer_sink_no_commit_eof
{
    std::span<mutable_buffer>
    prepare(std::span<mutable_buffer>)
    {
        return {};
    }

    mock_commit_awaitable
    commit(std::size_t)
    {
        return {};
    }
};

// Invalid: commit is not IoAwaitable
struct invalid_buffer_sink_not_io
{
    std::span<mutable_buffer>
    prepare(std::span<mutable_buffer>)
    {
        return {};
    }

    mock_commit_awaitable_not_io
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable_not_io
    commit_eof(std::size_t)
    {
        return {};
    }
};

// Invalid: prepare returns wrong type (size_t instead of span)
struct invalid_buffer_sink_prepare_returns_void
{
    void
    prepare(std::span<mutable_buffer>)
    {
    }

    mock_commit_awaitable
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable
    commit_eof(std::size_t)
    {
        return {};
    }
};

// Invalid: prepare has old signature (ptr + count)
struct invalid_buffer_sink_wrong_sig
{
    std::size_t
    prepare(mutable_buffer*, std::size_t) // Old signature
    {
        return 0;
    }

    mock_commit_awaitable
    commit(std::size_t)
    {
        return {};
    }

    mock_commit_awaitable
    commit_eof(std::size_t)
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
