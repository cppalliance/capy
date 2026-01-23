//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/write_sink.hpp>

#include <boost/system/error_code.hpp>

#include <cstddef>
#include <stop_token>
#include <tuple>
#include <utility>

namespace boost {
namespace capy {

namespace {

// Mock IoAwaitable returning system::error_code (for io_result<>)
struct mock_sink_awaitable
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        executor_ref,
        std::stop_token) const noexcept
    {
    }

    std::tuple<system::error_code>
    await_resume() const noexcept
    {
        return {};
    }
};

// Mock IoAwaitable with wrong return type (returns size_t too)
struct mock_sink_awaitable_wrong_type
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        executor_ref,
        std::stop_token) const noexcept
    {
    }

    std::pair<system::error_code, std::size_t>
    await_resume() const noexcept
    {
        return {};
    }
};

// Mock IoAwaitable returning int
struct mock_sink_awaitable_int
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        executor_ref,
        std::stop_token) const noexcept
    {
    }

    int await_resume() const noexcept { return 0; }
};

// Mock awaitable missing IoAwaitable protocol
struct mock_sink_awaitable_not_io
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(std::coroutine_handle<>) const noexcept {}

    std::tuple<system::error_code>
    await_resume() const noexcept
    {
        return {};
    }
};

//----------------------------------------------------------
// Mock sink types
//----------------------------------------------------------

// Valid WriteSink with templated write
struct valid_write_sink
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable
    write(CB const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Valid WriteSink accepting const_buffer directly (non-templated)
struct valid_write_sink_not_templated
{
    mock_sink_awaitable
    write(const_buffer const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Invalid: write returns wrong type (ec, size_t instead of ec)
struct invalid_write_sink_wrong_write_type
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_wrong_type
    write(CB const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Invalid: write_eof returns wrong type
struct invalid_write_sink_wrong_eof_type
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable
    write(CB const&)
    {
        return {};
    }

    mock_sink_awaitable_wrong_type
    write_eof()
    {
        return {};
    }
};

// Invalid: missing write
struct invalid_write_sink_no_write
{
    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Invalid: missing write_eof
struct invalid_write_sink_no_write_eof
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable
    write(CB const&)
    {
        return {};
    }
};

// Invalid: write is not IoAwaitable
struct invalid_write_sink_write_not_io
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_not_io
    write(CB const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Invalid: write_eof is not IoAwaitable
struct invalid_write_sink_eof_not_io
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable
    write(CB const&)
    {
        return {};
    }

    mock_sink_awaitable_not_io
    write_eof()
    {
        return {};
    }
};

// Invalid: write returns non-awaitable
struct invalid_write_sink_write_returns_int
{
    template<ConstBufferSequence CB>
    int write(CB const&) { return 0; }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

} // namespace

//----------------------------------------------------------
// Static assertions
//----------------------------------------------------------

// Valid sinks satisfy WriteSink
static_assert(WriteSink<valid_write_sink>);
static_assert(WriteSink<valid_write_sink_not_templated>);

// Wrong return types do not satisfy WriteSink
static_assert(!WriteSink<invalid_write_sink_wrong_write_type>);
static_assert(!WriteSink<invalid_write_sink_wrong_eof_type>);

// Missing methods do not satisfy WriteSink
static_assert(!WriteSink<invalid_write_sink_no_write>);
static_assert(!WriteSink<invalid_write_sink_no_write_eof>);

// Non-IoAwaitable does not satisfy WriteSink
static_assert(!WriteSink<invalid_write_sink_write_not_io>);
static_assert(!WriteSink<invalid_write_sink_eof_not_io>);

// Non-awaitable return does not satisfy WriteSink
static_assert(!WriteSink<invalid_write_sink_write_returns_int>);

} // namespace capy
} // namespace boost
