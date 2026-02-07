//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/read_stream.hpp>

#include <system_error>

#include <cstddef>
#include <stop_token>
#include <tuple>
#include <utility>

namespace boost {
namespace capy {

namespace {

// Mock IoAwaitable returning std::pair
struct mock_read_awaitable_pair
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
struct mock_read_awaitable_tuple
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
struct mock_read_awaitable_wrong_type
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
struct mock_read_awaitable_wrong_order
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
struct mock_read_awaitable_not_io
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

// Valid ReadStream with std::pair return (templated)
struct valid_read_stream_pair
{
    template<MutableBufferSequence MB>
    mock_read_awaitable_pair
    read_some(MB const&)
    {
        return {};
    }
};

// Valid ReadStream with std::tuple return (templated)
struct valid_read_stream_tuple
{
    template<MutableBufferSequence MB>
    mock_read_awaitable_tuple
    read_some(MB const&)
    {
        return {};
    }
};

// Valid: accepts mutable_buffer (workaround allows non-templated)
struct valid_read_stream_not_templated
{
    mock_read_awaitable_pair
    read_some(mutable_buffer const&)
    {
        return {};
    }
};

// Invalid: wrong return type
struct invalid_read_stream_wrong_type
{
    template<MutableBufferSequence MB>
    mock_read_awaitable_wrong_type
    read_some(MB const&)
    {
        return {};
    }
};

// Invalid: wrong decomposition order
struct invalid_read_stream_wrong_order
{
    template<MutableBufferSequence MB>
    mock_read_awaitable_wrong_order
    read_some(MB const&)
    {
        return {};
    }
};

// Invalid: not an IoAwaitable
struct invalid_read_stream_not_io
{
    template<MutableBufferSequence MB>
    mock_read_awaitable_not_io
    read_some(MB const&)
    {
        return {};
    }
};

// Invalid: missing read_some
struct invalid_read_stream_no_read_some
{
};

// Invalid: read_some returns non-awaitable
struct invalid_read_stream_returns_int
{
    template<MutableBufferSequence MB>
    int read_some(MB const&) { return 0; }
};

} // namespace

//----------------------------------------------------------
// Static assertions
//----------------------------------------------------------

// Valid streams satisfy ReadStream
static_assert(ReadStream<valid_read_stream_pair>);
static_assert(ReadStream<valid_read_stream_tuple>);

// Non-templated read_some satisfies ReadStream (workaround)
static_assert(ReadStream<valid_read_stream_not_templated>);

// Wrong return type does not satisfy ReadStream
static_assert(!ReadStream<invalid_read_stream_wrong_type>);

// Wrong decomposition order does not satisfy ReadStream
static_assert(!ReadStream<invalid_read_stream_wrong_order>);

// Non-IoAwaitable does not satisfy ReadStream
static_assert(!ReadStream<invalid_read_stream_not_io>);

// Missing read_some does not satisfy ReadStream
static_assert(!ReadStream<invalid_read_stream_no_read_some>);

// Non-awaitable return does not satisfy ReadStream
static_assert(!ReadStream<invalid_read_stream_returns_int>);

} // namespace capy
} // namespace boost
