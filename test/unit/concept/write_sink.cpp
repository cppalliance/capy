//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/write_sink.hpp>

#include <boost/capy/ex/io_env.hpp>

#include <system_error>

#include <cstddef>
#include <tuple>
#include <utility>

namespace boost {
namespace capy {

namespace {

// Mock IoAwaitable returning std::error_code (for write_eof())
struct mock_sink_awaitable
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        std::coroutine_handle<>,
        io_env const*) const noexcept
    {
    }

    std::tuple<std::error_code>
    await_resume() const noexcept
    {
        return {};
    }
};

// Mock IoAwaitable returning (error_code, size_t)
struct mock_sink_awaitable_with_size
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        std::coroutine_handle<>,
        io_env const*) const noexcept
    {
    }

    std::pair<std::error_code, std::size_t>
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
        std::coroutine_handle<>,
        io_env const*) const noexcept
    {
    }

    int await_resume() const noexcept { return 0; }
};

// Mock awaitable missing IoAwaitable protocol
struct mock_sink_awaitable_not_io
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(std::coroutine_handle<>) const noexcept {}

    std::tuple<std::error_code>
    await_resume() const noexcept
    {
        return {};
    }
};

// Mock awaitable missing IoAwaitable protocol, returns (ec, size_t)
struct mock_sink_awaitable_with_size_not_io
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
// Mock sink types
//----------------------------------------------------------

// Valid WriteSink: write_some + write + write_eof(buffers) + write_eof()
struct valid_write_sink
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_some(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_eof(CB const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Valid WriteSink with non-templated overloads
struct valid_write_sink_not_templated
{
    mock_sink_awaitable_with_size
    write_some(const_buffer const&)
    {
        return {};
    }

    mock_sink_awaitable_with_size
    write(const_buffer const&)
    {
        return {};
    }

    mock_sink_awaitable_with_size
    write_eof(const_buffer const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Invalid: missing write_some (does not satisfy WriteStream)
struct invalid_write_sink_no_write_some
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_eof(CB const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Invalid: write returns wrong type (ec instead of ec, size_t)
struct invalid_write_sink_wrong_write_type
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_some(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable
    write(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_eof(CB const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Invalid: write_eof() returns wrong type (ec,size_t instead of ec)
struct invalid_write_sink_wrong_eof_type
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_some(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_eof(CB const&)
    {
        return {};
    }

    mock_sink_awaitable_with_size
    write_eof()
    {
        return {};
    }
};

// Invalid: missing write
struct invalid_write_sink_no_write
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_some(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_eof(CB const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Invalid: missing write_eof() (bare)
struct invalid_write_sink_no_bare_eof
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_some(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_eof(CB const&)
    {
        return {};
    }
};

// Invalid: missing write_eof(buffers)
struct invalid_write_sink_no_write_eof_buffers
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_some(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
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

// Invalid: write is not IoAwaitable
struct invalid_write_sink_write_not_io
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_some(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size_not_io
    write(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_eof(CB const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Invalid: write_eof() is not IoAwaitable
struct invalid_write_sink_eof_not_io
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_some(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_eof(CB const&)
    {
        return {};
    }

    mock_sink_awaitable_not_io
    write_eof()
    {
        return {};
    }
};

// Invalid: write_eof(buffers) returns wrong type
struct invalid_write_sink_wrong_write_eof_buffers_type
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_some(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_sink_awaitable
    write_eof(CB const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Invalid: write returns non-awaitable
struct invalid_write_sink_write_returns_int
{
    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_some(CB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    int write(CB const&) { return 0; }

    template<ConstBufferSequence CB>
    mock_sink_awaitable_with_size
    write_eof(CB const&)
    {
        return {};
    }

    mock_sink_awaitable
    write_eof()
    {
        return {};
    }
};

// Invalid: empty type
struct invalid_write_sink_empty
{
};

} // namespace

//----------------------------------------------------------
// Static assertions
//----------------------------------------------------------

// Valid sinks satisfy WriteSink
static_assert(WriteSink<valid_write_sink>);
static_assert(WriteSink<valid_write_sink_not_templated>);

// Missing write_some does not satisfy WriteSink
static_assert(!WriteSink<invalid_write_sink_no_write_some>);

// Wrong return types do not satisfy WriteSink
static_assert(!WriteSink<invalid_write_sink_wrong_write_type>);
static_assert(!WriteSink<invalid_write_sink_wrong_eof_type>);
static_assert(!WriteSink<invalid_write_sink_wrong_write_eof_buffers_type>);

// Missing methods do not satisfy WriteSink
static_assert(!WriteSink<invalid_write_sink_no_write>);
static_assert(!WriteSink<invalid_write_sink_no_bare_eof>);
static_assert(!WriteSink<invalid_write_sink_no_write_eof_buffers>);
static_assert(!WriteSink<invalid_write_sink_empty>);

// Non-IoAwaitable does not satisfy WriteSink
static_assert(!WriteSink<invalid_write_sink_write_not_io>);
static_assert(!WriteSink<invalid_write_sink_eof_not_io>);

// Non-awaitable return does not satisfy WriteSink
static_assert(!WriteSink<invalid_write_sink_write_returns_int>);

} // namespace capy
} // namespace boost
