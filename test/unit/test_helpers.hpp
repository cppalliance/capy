//
// Copyright (c) 2021 Vinnie Falco (vinnie dot falco at gmail dot com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_HELPERS_HPP
#define BOOST_CAPY_TEST_HELPERS_HPP

#include <boost/capy/concept/executor.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/execution_context.hpp>

#include "test_suite.hpp"

#include <chrono>
#include <cstring>
#include <thread>

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#include <pthread.h>
#define BOOST_CAPY_TEST_CAN_GET_THREAD_NAME 1
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define BOOST_CAPY_TEST_CAN_GET_THREAD_NAME 1
#endif

namespace boost {
namespace capy {

//----------------------------------------------------------
// Test Utilities
//----------------------------------------------------------

class test_context : public execution_context
{
public:
    int id = 0;
};

inline test_context&
default_test_context() noexcept
{
    static test_context ctx;
    return ctx;
}

/** Simple synchronous executor for testing.

    Satisfies the Executor concept.
    Executes inline (returns the handle for symmetric transfer).
    Uses pointers to external storage to allow copying.
*/
struct test_executor
{
    int id_ = 0;
    int* dispatch_count_ = nullptr;
    test_context* ctx_ = nullptr;

    test_executor() = default;

    explicit
    test_executor(test_context& ctx) noexcept
        : ctx_(&ctx)
    {
    }

    explicit
    test_executor(int& count) noexcept
        : dispatch_count_(&count)
    {
    }

    test_executor(int id, int& count) noexcept
        : id_(id)
        , dispatch_count_(&count)
    {
    }

    bool
    operator==(test_executor const& other) const noexcept
    {
        return id_ == other.id_ &&
               dispatch_count_ == other.dispatch_count_ &&
               ctx_ == other.ctx_;
    }

    execution_context&
    context() const noexcept
    {
        return ctx_ ? *ctx_ : default_test_context();
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    coro
    dispatch(coro h) const
    {
        if(dispatch_count_)
            ++(*dispatch_count_);
        return h;
    }

    void
    post(coro h) const
    {
        h.resume();
    }
};

static_assert(Executor<test_executor>);

//----------------------------------------------------------
// Wait Utilities
//----------------------------------------------------------

/// Wait for a predicate to become true with timeout.
template<class Pred>
bool
wait_for(
    Pred pred,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
{
    auto start = std::chrono::steady_clock::now();
    while(!pred())
    {
        if(std::chrono::steady_clock::now() - start > timeout)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

//----------------------------------------------------------
// Thread Name Utilities
//----------------------------------------------------------

#if defined(BOOST_CAPY_TEST_CAN_GET_THREAD_NAME)

/// Get current thread name into buffer, returns true on success.
inline bool
get_current_thread_name(char* buffer, std::size_t size)
{
#if defined(_WIN32)
    wchar_t* wname = nullptr;
    if(!SUCCEEDED(GetThreadDescription(GetCurrentThread(), &wname)))
        return false;
    int len = WideCharToMultiByte(
        CP_UTF8, 0, wname, -1, buffer,
        static_cast<int>(size), nullptr, nullptr);
    LocalFree(wname);
    return len > 0;
#else
    return pthread_getname_np(pthread_self(), buffer, size) == 0;
#endif
}

/// Check if current thread name matches expected value exactly.
inline bool
check_thread_name(char const* expected)
{
    char buffer[64] = {};
#if defined(_WIN32)
    wchar_t* wname = nullptr;
    if(!SUCCEEDED(GetThreadDescription(GetCurrentThread(), &wname)))
        return false;
    int len = WideCharToMultiByte(
        CP_UTF8, 0, wname, -1, buffer,
        static_cast<int>(sizeof(buffer)), nullptr, nullptr);
    LocalFree(wname);
    if(len <= 0)
        return false;
#else
    if(pthread_getname_np(pthread_self(), buffer, sizeof(buffer)) != 0)
        return false;
#endif
    return std::strcmp(buffer, expected) == 0;
}

/// Check if current thread name starts with given prefix.
inline bool
thread_name_starts_with(char const* name_prefix)
{
    char buffer[64] = {};
    if(!get_current_thread_name(buffer, sizeof(buffer)))
        return false;
    return std::strncmp(buffer, name_prefix, std::strlen(name_prefix)) == 0;
}

#endif // BOOST_CAPY_TEST_CAN_GET_THREAD_NAME

} // capy
} // boost

#endif
