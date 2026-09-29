//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/detail/await_suspend_helper.hpp>

#include <boost/capy/ex/io_env.hpp>

#include <coroutine>
#include <cstddef>
#include <new>

#ifdef _WIN32
# include <windows.h>
#else
# include <sys/mman.h>
#endif

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace detail {

namespace {

bool probe_continuation_ran = false;

// Coroutine frames for the destroy-then-transfer probe, allocated so
// that destroying a frame UNMAPS it. Any access to the frame after
// destroy - read or write - then faults.
//
// A poisoning allocator is not sufficient. Routing the return through
// symmetric_transfer moves the compiler's write of the handle into the
// frame slot to after the frame is destroyed, which repairs a poison
// pattern and hides the defect. Unmapping cannot be repaired, so this
// probe sees the frame access that poisoning misses.
struct probe_frame
{
    static void* allocate(std::size_t n) noexcept
    {
    #ifdef _WIN32
        return ::VirtualAlloc(
            nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    #else
        void* const p = ::mmap(nullptr, n, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return p == MAP_FAILED ? nullptr : p;
    #endif
    }

    static void release(void* p, std::size_t n) noexcept
    {
    #ifdef _WIN32
        (void)n;
        ::VirtualFree(p, 0, MEM_RELEASE);
    #else
        ::munmap(p, n);
    #endif
    }
};

struct probe_task
{
    struct promise_type
    {
        void* operator new(std::size_t n)
        {
            void* const p = probe_frame::allocate(n);
            if(! p)
                throw std::bad_alloc();
            return p;
        }

        void operator delete(void* p, std::size_t n) noexcept
        {
            probe_frame::release(p, n);
        }

        probe_task get_return_object() noexcept
        {
            return { std::coroutine_handle<
                promise_type>::from_promise(*this) };
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { BOOST_TEST(false); }
    };

    std::coroutine_handle<promise_type> h;
};

// Mirrors the final_suspend awaiters in when_all_runner and
// when_any_runner: destroy our own frame, then transfer to the
// continuation. auto return type, because symmetric_transfer
// returns void on the workaround path.
struct destroy_then_transfer
{
    std::coroutine_handle<> next;

    bool await_ready() const noexcept { return false; }

    auto await_suspend(std::coroutine_handle<> self) noexcept
    {
        // Copy to the stack first: this awaiter lives on the frame
        // about to be destroyed.
        auto const continuation = next;
        self.destroy();
        return symmetric_transfer(continuation);
    }

    void await_resume() const noexcept {}
};

probe_task probe_continuation()
{
    probe_continuation_ran = true;
    co_return;
}

probe_task probe_victim(std::coroutine_handle<> next)
{
    co_await destroy_then_transfer{ next };
}

} // (anon)

class await_suspend_helper_test
{
    // await_suspend returning void: caller suspends unconditionally.
    struct void_awaitable
    {
        bool suspended = false;
        void await_suspend(std::coroutine_handle<>, io_env const*)
        {
            suspended = true;
        }
    };

    // await_suspend returning bool: true suspends, false resumes.
    struct bool_awaitable
    {
        bool value;
        bool await_suspend(std::coroutine_handle<>, io_env const*)
        {
            return value;
        }
    };

    // await_suspend returning a handle: symmetric transfer to it.
    template <typename P>
    struct handle_awaitable
    {
        std::coroutine_handle<P> next;
        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<>, io_env const*)
        {
            return next;
        }
    };

public:
    // capy#378: transferring out of an await_suspend that has already
    // destroyed its own frame must not touch that frame afterwards.
    // On affected MSVC toolsets symmetric_transfer resumes on the
    // current stack to avoid the frame round-trip; elsewhere it
    // performs a real tail-call. Either way the continuation must run
    // and the process must survive.
    //
    // When the gate is wrong for the compiler in use this fails as a
    // hard access violation rather than a BOOST_TEST failure. Each
    // test runs in its own process under CTest, so the crash is
    // reported as this test failing.
    void
    testDestroyThenTransfer()
    {
        probe_continuation_ran = false;

        auto cont = probe_continuation();
        auto victim = probe_victim(cont.h);

        victim.h.resume();

        BOOST_TEST(probe_continuation_ran);

        cont.h.destroy();
    }

    void
    run()
    {
        testDestroyThenTransfer();

        auto const h = std::noop_coroutine();

        {
            // void -> noop_coroutine, and the awaitable was invoked.
            void_awaitable va;
            BOOST_TEST(call_await_suspend(&va, h, nullptr) == h);
            BOOST_TEST(va.suspended);
        }

        {
            // bool true -> noop_coroutine (stay suspended).
            bool_awaitable bt{true};
            BOOST_TEST(call_await_suspend(&bt, h, nullptr) == h);
        }

        {
            // bool false -> the original handle (resume).
            bool_awaitable bf{false};
            BOOST_TEST(call_await_suspend(&bf, h, nullptr) == h);
        }

        {
            // handle<void> -> the returned handle.
            handle_awaitable<void> hv{h};
            BOOST_TEST(call_await_suspend(&hv, h, nullptr) == h);
        }

        {
            // handle<P> -> the returned handle.
            handle_awaitable<std::noop_coroutine_promise> hp{h};
            BOOST_TEST(call_await_suspend(&hp, h, nullptr) == h);
        }
    }
};

TEST_SUITE(
    await_suspend_helper_test,
    "boost.capy.detail.await_suspend_helper");

} // detail
} // capy
} // boost
