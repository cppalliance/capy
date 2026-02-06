//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/read_source.hpp>

#include <system_error>

#include <cstddef>
#include <stop_token>
#include <tuple>
#include <utility>

namespace boost {
namespace capy {

namespace {

// Mock IoAwaitable returning (error_code, size_t) as pair
struct mock_source_awaitable_pair
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

// Mock IoAwaitable returning (error_code, size_t) as tuple
struct mock_source_awaitable_tuple
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

// Mock IoAwaitable with wrong return type (just error_code)
struct mock_source_awaitable_wrong_type
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

// Mock IoAwaitable returning int
struct mock_source_awaitable_int
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

// Valid ReadSource with both read_some and read (pair)
struct valid_read_source_pair
{
    template<MutableBufferSequence MB>
    mock_source_awaitable_pair
    read_some(MB const&)
    {
        return {};
    }

    template<MutableBufferSequence MB>
    mock_source_awaitable_pair
    read(MB const&)
    {
        return {};
    }
};

// Valid ReadSource with both read_some and read (tuple)
struct valid_read_source_tuple
{
    template<MutableBufferSequence MB>
    mock_source_awaitable_tuple
    read_some(MB const&)
    {
        return {};
    }

    template<MutableBufferSequence MB>
    mock_source_awaitable_tuple
    read(MB const&)
    {
        return {};
    }
};

// Valid ReadSource accepting mutable_buffer directly (non-templated)
struct valid_read_source_not_templated
{
    mock_source_awaitable_pair
    read_some(mutable_buffer const&)
    {
        return {};
    }

    mock_source_awaitable_pair
    read(mutable_buffer const&)
    {
        return {};
    }
};

// Invalid: has read but no read_some (does not satisfy ReadStream)
struct invalid_read_source_no_read_some
{
    template<MutableBufferSequence MB>
    mock_source_awaitable_pair
    read(MB const&)
    {
        return {};
    }
};

// Invalid: has read_some but no read
struct invalid_read_source_no_read
{
    template<MutableBufferSequence MB>
    mock_source_awaitable_pair
    read_some(MB const&)
    {
        return {};
    }
};

// Invalid: read returns wrong type (just ec instead of ec, size_t)
struct invalid_read_source_wrong_type
{
    template<MutableBufferSequence MB>
    mock_source_awaitable_pair
    read_some(MB const&)
    {
        return {};
    }

    template<MutableBufferSequence MB>
    mock_source_awaitable_wrong_type
    read(MB const&)
    {
        return {};
    }
};

// Invalid: missing both read_some and read
struct invalid_read_source_empty
{
};

// Invalid: read is not IoAwaitable
struct invalid_read_source_not_io
{
    template<MutableBufferSequence MB>
    mock_source_awaitable_pair
    read_some(MB const&)
    {
        return {};
    }

    template<MutableBufferSequence MB>
    mock_source_awaitable_not_io
    read(MB const&)
    {
        return {};
    }
};

// Invalid: read returns non-awaitable
struct invalid_read_source_returns_int
{
    template<MutableBufferSequence MB>
    mock_source_awaitable_pair
    read_some(MB const&)
    {
        return {};
    }

    template<MutableBufferSequence MB>
    int read(MB const&) { return 0; }
};

} // namespace

//----------------------------------------------------------
// Static assertions
//----------------------------------------------------------

// Valid sources satisfy ReadSource
static_assert(ReadSource<valid_read_source_pair>);
static_assert(ReadSource<valid_read_source_tuple>);
static_assert(ReadSource<valid_read_source_not_templated>);

// Has read but no read_some: does not satisfy ReadSource
static_assert(!ReadSource<invalid_read_source_no_read_some>);

// Has read_some but no read: does not satisfy ReadSource
static_assert(!ReadSource<invalid_read_source_no_read>);

// Wrong return type does not satisfy ReadSource
static_assert(!ReadSource<invalid_read_source_wrong_type>);

// Missing everything does not satisfy ReadSource
static_assert(!ReadSource<invalid_read_source_empty>);

// Non-IoAwaitable does not satisfy ReadSource
static_assert(!ReadSource<invalid_read_source_not_io>);

// Non-awaitable return does not satisfy ReadSource
static_assert(!ReadSource<invalid_read_source_returns_int>);

} // namespace capy
} // namespace boost
