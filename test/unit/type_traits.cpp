//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/type_traits.hpp>

#include <boost/system/error_code.hpp>

#include <array>
#include <coroutine>
#include <cstddef>
#include <string>
#include <tuple>
#include <utility>

namespace boost {
namespace capy {

// decomposes_to with std::pair
static_assert(decomposes_to<std::pair<int, double>, int, double>);
static_assert(!decomposes_to<std::pair<int, double>, double, int>);
static_assert(!decomposes_to<std::pair<int, double>, int>);
static_assert(!decomposes_to<std::pair<int, double>, int, double, char>);

// decomposes_to with std::tuple
static_assert(decomposes_to<std::tuple<int>, int>);
static_assert(decomposes_to<std::tuple<int, float, char>, int, float, char>);
static_assert(!decomposes_to<std::tuple<int, float, char>, int, float>);
static_assert(decomposes_to<std::tuple<>>);

// decomposes_to with std::array
static_assert(decomposes_to<std::array<int, 2>, int, int>);
static_assert(decomposes_to<std::array<int, 3>, int, int, int>);
static_assert(!decomposes_to<std::array<int, 2>, int>);

// decomposes_to with cv-qualified and reference types
static_assert(decomposes_to<std::pair<int, double> const, int, double>);
static_assert(decomposes_to<std::pair<int, double>&, int, double>);
static_assert(decomposes_to<std::pair<int, double> const&, int, double>);

// decomposes_to with real-world types
static_assert(decomposes_to<
    std::pair<system::error_code, std::size_t>,
    system::error_code, std::size_t>);

// Mock awaitable for testing awaitable_decomposes_to
struct mock_pair_awaitable
{
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    std::pair<system::error_code, std::size_t> await_resume() const noexcept
    {
        return {};
    }
};

struct mock_tuple_awaitable
{
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    std::tuple<int, std::string, double> await_resume() const
    {
        return {};
    }
};

struct mock_int_awaitable
{
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    int await_resume() const noexcept { return 0; }
};

// Mock awaitable with operator co_await
struct mock_with_co_await_op
{
    mock_pair_awaitable operator co_await() const noexcept
    {
        return {};
    }
};

// awaitable_decomposes_to tests
static_assert(awaitable_decomposes_to<
    mock_pair_awaitable,
    system::error_code, std::size_t>);

static_assert(awaitable_decomposes_to<
    mock_tuple_awaitable,
    int, std::string, double>);

static_assert(!awaitable_decomposes_to<
    mock_pair_awaitable,
    int, double>);

static_assert(!awaitable_decomposes_to<
    mock_tuple_awaitable,
    int, std::string>);

// awaitable_decomposes_to with operator co_await
static_assert(awaitable_decomposes_to<
    mock_with_co_await_op,
    system::error_code, std::size_t>);

} // namespace capy
} // namespace boost
