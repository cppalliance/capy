//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/stream.hpp>

#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io/any_stream.hpp>

#include <system_error>

#include <cstddef>
#include <tuple>
#include <utility>

namespace boost {
namespace capy {

namespace {

//----------------------------------------------------------
// Mock awaitables
//----------------------------------------------------------

// Mock IoAwaitable returning std::pair for read
struct mock_read_awaitable
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        io_env const&) const noexcept
    {
    }

    std::pair<std::error_code, std::size_t>
    await_resume() const noexcept
    {
        return {};
    }
};

// Mock IoAwaitable returning std::pair for write
struct mock_write_awaitable
{
    bool await_ready() const noexcept { return true; }

    void await_suspend(
        coro,
        io_env const&) const noexcept
    {
    }

    std::pair<std::error_code, std::size_t>
    await_resume() const noexcept
    {
        return {};
    }
};

//----------------------------------------------------------
// Mock stream types
//----------------------------------------------------------

// Valid Stream: satisfies both ReadStream and WriteStream
struct valid_stream
{
    template<MutableBufferSequence MB>
    mock_read_awaitable
    read_some(MB const&)
    {
        return {};
    }

    template<ConstBufferSequence CB>
    mock_write_awaitable
    write_some(CB const&)
    {
        return {};
    }
};

// Read-only stream: satisfies ReadStream but not WriteStream
struct read_only_stream
{
    template<MutableBufferSequence MB>
    mock_read_awaitable
    read_some(MB const&)
    {
        return {};
    }
};

// Write-only stream: satisfies WriteStream but not ReadStream
struct write_only_stream
{
    template<ConstBufferSequence CB>
    mock_write_awaitable
    write_some(CB const&)
    {
        return {};
    }
};

// Neither: satisfies neither ReadStream nor WriteStream
struct neither_stream
{
};

} // namespace

//----------------------------------------------------------
// Static assertions
//----------------------------------------------------------

// Valid streams satisfy Stream
static_assert(Stream<valid_stream>);

// Read-only does not satisfy Stream
static_assert(!Stream<read_only_stream>);

// Write-only does not satisfy Stream
static_assert(!Stream<write_only_stream>);

// Neither does not satisfy Stream
static_assert(!Stream<neither_stream>);

// any_stream satisfies Stream
static_assert(Stream<any_stream>);

} // namespace capy
} // namespace boost
