//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/write_stream.hpp>

#include <system_error>

#include <cstddef>
#include <stop_token>
#include <tuple>
#include <utility>

namespace boost {
namespace capy {

namespace {

// Mock IoAwaitable returning std::pair
struct mock_write_awaitable_pair
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

// Mock IoAwaitable returning std::tuple
struct mock_write_awaitable_tuple
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        executor_ref,
        std::stop_token) const noexcept
    {
    }

    std::tuple<std::error_code, std::size_t>
    await_resume() const noexcept
    {
        return {};
    }
};

// Mock IoAwaitable with wrong return type
struct mock_write_awaitable_wrong_type
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

// Mock IoAwaitable with wrong decomposition order
struct mock_write_awaitable_wrong_order
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        executor_ref,
        std::stop_token) const noexcept
    {
    }

    std::pair<std::size_t, std::error_code>
    await_resume() const noexcept
    {
        return {};
    }
};

// Mock awaitable missing IoAwaitable protocol
struct mock_write_awaitable_not_io
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
// Mock stream types
//----------------------------------------------------------

// Valid WriteStream with std::pair return (templated)
struct valid_write_stream_pair
{
    template<ConstBufferSequence CB>
    mock_write_awaitable_pair
    write_some(CB const&)
    {
        return {};
    }
};

// Valid WriteStream with std::tuple return (templated)
struct valid_write_stream_tuple
{
    template<ConstBufferSequence CB>
    mock_write_awaitable_tuple
    write_some(CB const&)
    {
        return {};
    }
};

// Valid: accepts const_buffer (workaround allows non-templated)
struct valid_write_stream_not_templated
{
    mock_write_awaitable_pair
    write_some(const_buffer const&)
    {
        return {};
    }
};

// Invalid: wrong return type
struct invalid_write_stream_wrong_type
{
    template<ConstBufferSequence CB>
    mock_write_awaitable_wrong_type
    write_some(CB const&)
    {
        return {};
    }
};

// Invalid: wrong decomposition order
struct invalid_write_stream_wrong_order
{
    template<ConstBufferSequence CB>
    mock_write_awaitable_wrong_order
    write_some(CB const&)
    {
        return {};
    }
};

// Invalid: not an IoAwaitable
struct invalid_write_stream_not_io
{
    template<ConstBufferSequence CB>
    mock_write_awaitable_not_io
    write_some(CB const&)
    {
        return {};
    }
};

// Invalid: missing write_some
struct invalid_write_stream_no_write_some
{
};

// Invalid: write_some returns non-awaitable
struct invalid_write_stream_returns_int
{
    template<ConstBufferSequence CB>
    int write_some(CB const&) { return 0; }
};

} // namespace

//----------------------------------------------------------
// Static assertions
//----------------------------------------------------------

// Valid streams satisfy WriteStream
static_assert(WriteStream<valid_write_stream_pair>);
static_assert(WriteStream<valid_write_stream_tuple>);

// Non-templated write_some satisfies WriteStream (workaround)
static_assert(WriteStream<valid_write_stream_not_templated>);

// Wrong return type does not satisfy WriteStream
static_assert(!WriteStream<invalid_write_stream_wrong_type>);

// Wrong decomposition order does not satisfy WriteStream
static_assert(!WriteStream<invalid_write_stream_wrong_order>);

// Non-IoAwaitable does not satisfy WriteStream
static_assert(!WriteStream<invalid_write_stream_not_io>);

// Missing write_some does not satisfy WriteStream
static_assert(!WriteStream<invalid_write_stream_no_write_some>);

// Non-awaitable return does not satisfy WriteStream
static_assert(!WriteStream<invalid_write_stream_returns_int>);

} // namespace capy
} // namespace boost
