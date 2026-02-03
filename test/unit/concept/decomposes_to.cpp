//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/concept/decomposes_to.hpp>

#include <system_error>

#include <array>
#include <coroutine>
#include <cstddef>
#include <string>
#include <tuple>
#include <utility>

namespace boost {
namespace capy {

// Aggregate types for testing structured bindings
struct agg0 { };
struct agg1 { int a; };
struct agg2 { int a; double b; };
struct agg3 { int a; float b; char c; };
struct agg4 { int a; float b; char c; double d; };
struct agg5 { int a; float b; char c; double d; long e; };

// Aggregate types with complex members
struct xgg1 { std::error_code ec; };
struct xgg2 { std::error_code ec; std::size_t n; };
struct xgg3 { std::error_code ec; std::string s; double d; };
struct xgg4 { std::error_code ec; std::string s; std::size_t n; int* p; };

// decomposes_to with aggregates (arity 0)
static_assert(decomposes_to<agg0>);
static_assert(!decomposes_to<agg0, int>);

// decomposes_to with aggregates (arity 1)
static_assert(decomposes_to<agg1, int>);
static_assert(!decomposes_to<agg1, double>);
static_assert(!decomposes_to<agg1, int, int>);

// decomposes_to with aggregates (arity 2)
static_assert(decomposes_to<agg2, int, double>);
static_assert(!decomposes_to<agg2, double, int>);
static_assert(!decomposes_to<agg2, int>);
static_assert(!decomposes_to<agg2, int, double, char>);

// decomposes_to with aggregates (arity 3)
static_assert(decomposes_to<agg3, int, float, char>);
static_assert(!decomposes_to<agg3, int, float>);
static_assert(!decomposes_to<agg3, int, float, char, double>);

// decomposes_to with aggregates (arity 4)
static_assert(decomposes_to<agg4, int, float, char, double>);
static_assert(!decomposes_to<agg4, int, float, char>);
static_assert(!decomposes_to<agg4, int, float, char, double, long>);

// decomposes_to with aggregates (arity 5, unsupported)
static_assert(!decomposes_to<agg5, int, float, char, double, long>);
static_assert(!decomposes_to<agg5, int, float, char, double>);

// decomposes_to with cv-qualified aggregates
static_assert(decomposes_to<agg2 const, int, double>);
static_assert(decomposes_to<agg2&, int, double>);
static_assert(decomposes_to<agg2 const&, int, double>);
static_assert(decomposes_to<agg4 const&, int, float, char, double>);

// decomposes_to with complex aggregates (arity 1)
static_assert(decomposes_to<xgg1, std::error_code>);
static_assert(!decomposes_to<xgg1, int>);

// decomposes_to with complex aggregates (arity 2)
static_assert(decomposes_to<xgg2, std::error_code, std::size_t>);
static_assert(!decomposes_to<xgg2, std::size_t, std::error_code>);

// decomposes_to with complex aggregates (arity 3)
static_assert(decomposes_to<xgg3, std::error_code, std::string, double>);
static_assert(!decomposes_to<xgg3, std::error_code, std::string>);

// decomposes_to with complex aggregates (arity 4)
static_assert(decomposes_to<xgg4, std::error_code, std::string, std::size_t, int*>);
static_assert(!decomposes_to<xgg4, std::error_code, std::string, std::size_t>);

// decomposes_to with cv-qualified complex aggregates
static_assert(decomposes_to<xgg2 const, std::error_code, std::size_t>);
static_assert(decomposes_to<xgg2&, std::error_code, std::size_t>);
static_assert(decomposes_to<xgg2 const&, std::error_code, std::size_t>);
static_assert(decomposes_to<xgg4 const&, std::error_code, std::string, std::size_t, int*>);

// decomposes_to with std::pair (arity 2)
static_assert(decomposes_to<std::pair<int, double>, int, double>);
static_assert(!decomposes_to<std::pair<int, double>, double, int>);
static_assert(!decomposes_to<std::pair<int, double>, int>);
static_assert(!decomposes_to<std::pair<int, double>, int, double, char>);

// decomposes_to with std::tuple (arity 1)
static_assert(decomposes_to<std::tuple<int>, int>);
static_assert(!decomposes_to<std::tuple<int>, double>);

// decomposes_to with std::tuple (arity 2)
static_assert(decomposes_to<std::tuple<int, float>, int, float>);

// decomposes_to with std::tuple (arity 3)
static_assert(decomposes_to<std::tuple<int, float, char>, int, float, char>);
static_assert(!decomposes_to<std::tuple<int, float, char>, int, float>);

// decomposes_to with std::tuple (arity 4)
static_assert(decomposes_to<std::tuple<int, float, char, double>, int, float, char, double>);
static_assert(!decomposes_to<std::tuple<int, float, char, double>, int, float, char>);
static_assert(!decomposes_to<std::tuple<int, float, char, double>, int, float, char, double, long>);

// decomposes_to with empty tuple (arity 0)
static_assert(decomposes_to<std::tuple<>>);

// decomposes_to with std::array (arity 2)
static_assert(decomposes_to<std::array<int, 2>, int, int>);
static_assert(!decomposes_to<std::array<int, 2>, int>);

// decomposes_to with std::array (arity 3)
static_assert(decomposes_to<std::array<int, 3>, int, int, int>);

// decomposes_to with std::array (arity 4)
static_assert(decomposes_to<std::array<int, 4>, int, int, int, int>);
static_assert(!decomposes_to<std::array<int, 4>, int, int, int>);

// decomposes_to with cv-qualified and reference types
static_assert(decomposes_to<std::pair<int, double> const, int, double>);
static_assert(decomposes_to<std::pair<int, double>&, int, double>);
static_assert(decomposes_to<std::pair<int, double> const&, int, double>);
static_assert(decomposes_to<std::tuple<int, float, char, double> const&, int, float, char, double>);

// decomposes_to with real-world types
static_assert(decomposes_to<
    std::pair<std::error_code, std::size_t>,
    std::error_code, std::size_t>);

// Mock awaitable for testing awaitable_decomposes_to
struct mock_pair_awaitable
{
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    std::pair<std::error_code, std::size_t> await_resume() const noexcept
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

struct mock_agg2_awaitable
{
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    agg2 await_resume() const noexcept { return {}; }
};

struct mock_agg4_awaitable
{
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    agg4 await_resume() const noexcept { return {}; }
};

struct mock_xgg2_awaitable
{
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    xgg2 await_resume() const noexcept { return {}; }
};

struct mock_xgg4_awaitable
{
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    xgg4 await_resume() const { return {}; }
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
    std::error_code, std::size_t>);

static_assert(awaitable_decomposes_to<
    mock_tuple_awaitable,
    int, std::string, double>);

static_assert(!awaitable_decomposes_to<
    mock_pair_awaitable,
    int, double>);

static_assert(!awaitable_decomposes_to<
    mock_tuple_awaitable,
    int, std::string>);

// awaitable_decomposes_to with aggregates (arity 2)
static_assert(awaitable_decomposes_to<
    mock_agg2_awaitable,
    int, double>);

static_assert(!awaitable_decomposes_to<
    mock_agg2_awaitable,
    double, int>);

// awaitable_decomposes_to with aggregates (arity 4)
static_assert(awaitable_decomposes_to<
    mock_agg4_awaitable,
    int, float, char, double>);

static_assert(!awaitable_decomposes_to<
    mock_agg4_awaitable,
    int, float, char>);

static_assert(!awaitable_decomposes_to<
    mock_agg4_awaitable,
    int, float, char, double, long>);

// awaitable_decomposes_to with complex aggregates (arity 2)
static_assert(awaitable_decomposes_to<
    mock_xgg2_awaitable,
    std::error_code, std::size_t>);

static_assert(!awaitable_decomposes_to<
    mock_xgg2_awaitable,
    std::size_t, std::error_code>);

// awaitable_decomposes_to with complex aggregates (arity 4)
static_assert(awaitable_decomposes_to<
    mock_xgg4_awaitable,
    std::error_code, std::string, std::size_t, int*>);

static_assert(!awaitable_decomposes_to<
    mock_xgg4_awaitable,
    std::error_code, std::string, std::size_t>);

// awaitable_decomposes_to with operator co_await
static_assert(awaitable_decomposes_to<
    mock_with_co_await_op,
    std::error_code, std::size_t>);

// uncomment this to see what diagnostics look like
#if 0
struct bad_agg
{
    std::error_code ec;
    std::string s;
};

struct bad_aw
{
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    bad_agg await_resume() const { return {}; }
};
#endif

} // namespace capy
} // namespace boost
