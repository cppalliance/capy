//
// Copyright (c) 2021 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_HELPERS_HPP
#define BOOST_CAPY_TEST_HELPERS_HPP

#include <boost/capy/concept/execution_context.hpp>
#include <boost/capy/concept/executor.hpp>
#include <coroutine>
#include <boost/capy/ex/abandon.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/task.hpp>

#include <atomic>
#include <stop_token>

#include "test_suite.hpp"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <iterator>
#include <new>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

// Valgrind slows execution ~10-20x; scale timing-sensitive
// durations to avoid false failures.
#ifdef BOOST_NO_STRESS_TEST
inline constexpr int failsafe_scale = 20;
#else
inline constexpr int failsafe_scale = 1;
#endif

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

class test_io_context;

inline test_io_context&
default_test_io_context() noexcept;

/** Simple synchronous executor for testing.

    Satisfies the Executor concept.
    Executes inline (returns the handle for symmetric transfer).
    Uses pointers to external storage to allow copying.
*/
struct test_executor
{
    int id_ = 0;
    int* dispatch_count_ = nullptr;
    test_io_context* ctx_ = nullptr;

    test_executor() = default;

    explicit
    test_executor(test_io_context& ctx) noexcept
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

    test_io_context&
    context() const noexcept;

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<>
    dispatch(continuation& c) const
    {
        if(dispatch_count_)
            ++(*dispatch_count_);
        return c.h;
    }

    void
    post(continuation& c) const
    {
        c.h.resume();
    }
};

/** Test execution context satisfying ExecutionContext.

    Provides a minimal execution context for testing that
    satisfies the ExecutionContext concept requirements.
*/
class test_io_context : public execution_context
{
public:
    using executor_type = test_executor;

    int id = 0;

    test_io_context()
        : execution_context(this)
    {
    }

    executor_type
    get_executor() noexcept
    {
        return test_executor(*this);
    }
};

inline test_io_context&
default_test_io_context() noexcept
{
    static test_io_context ctx;
    return ctx;
}

inline test_io_context&
test_executor::context() const noexcept
{
    return ctx_ ? *ctx_ : default_test_io_context();
}

static_assert(Executor<test_executor>);
static_assert(ExecutionContext<test_io_context>);

/// A test context whose services the test shuts down directly.
class closable_test_context : public test_io_context
{
public:
    using execution_context::shutdown;
};


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

/** Gives up its own coroutine as a holder would.

    The awaiting coroutine is abandoned from inside `await_suspend`, so
    its chain is unwound without ever resuming.
*/
struct self_destroy_awaitable
{
    bool await_ready() {return false;}

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, io_env const* env)
    {
        continuation c{h, nullptr, env->owner};
        capy::abandon(c);
        return std::noop_coroutine();
    }
    void await_resume() {}
};


// Callable that posts a continuation to an executor instead of
// resuming a coroutine handle inline.  Use as the callback type
// for std::stop_callback — direct resumption runs the coroutine
// on whatever thread calls request_stop(), bypassing the executor.
struct resume_via_post
{
    executor_ref ex;
    mutable continuation cont;

    void operator()() const noexcept
    {
        ex.post(cont);
    }
};

using stop_resume_callback = std::stop_callback<resume_via_post>;

inline resume_via_post
post_resume(
    io_env const& env,
    std::coroutine_handle<> h) noexcept
{
    return resume_via_post{env.executor, continuation{h, nullptr, env.owner}};
}

// test awaitable that must be stopped in order to resume.
// Uses resume_via_post to ensure the coroutine resumes on the
// executor's thread, not on whatever thread calls request_stop().
struct stop_only_awaitable
{
    stop_only_awaitable() noexcept = default;
    stop_only_awaitable(stop_only_awaitable && ) noexcept {}

    // Placement-new storage instead of std::optional to avoid a
    // data race on optional's _M_engaged flag.  The stop_callback
    // constructor synchronises with request_stop() through the
    // stop-state's atomics, but optional::emplace writes _M_engaged
    // *after* the constructor returns — outside that sync window.
    // When ~jthread calls request_stop() before join(), the
    // destructor's _M_reset (on the requesting thread) races with
    // emplace's _M_engaged write (on the registering thread).
    BOOST_CAPY_MSVC_WARNING_PUSH
    BOOST_CAPY_MSVC_WARNING_DISABLE(4324) // padded due to alignas
    alignas(stop_resume_callback)
        unsigned char stop_cb_buf_[sizeof(stop_resume_callback)]{};
    BOOST_CAPY_MSVC_WARNING_POP
    std::atomic<bool> active_{false};

    ~stop_only_awaitable()
    {
        if (active_.load(std::memory_order_acquire))
            reinterpret_cast<stop_resume_callback*>(
                stop_cb_buf_)->~stop_resume_callback();
    }

    bool await_ready() {return false;}

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, io_env const* env)
    {
        if (env->stop_token.stop_requested())
            return h;
        ::new(stop_cb_buf_) stop_resume_callback(
            env->stop_token, post_resume(*env, h));
        active_.store(true, std::memory_order_release);
        return std::noop_coroutine();
    }
    void await_resume() {}
};

//----------------------------------------------------------
// Test Exception Types
//----------------------------------------------------------

/** Standard test exception type used across test files. */
struct test_exception : std::runtime_error
{
    explicit test_exception(const char* msg)
        : std::runtime_error(msg)
    {
    }
};

/** Helper to throw test_exception with a message. */
[[noreturn]] inline void
throw_test_exception(char const* msg)
{
    throw test_exception(msg);
}

// Throw test_exception in a way the optimizer cannot prove always
// happens. Used by throwing move-constructors in the type-erasure
// tests: an unconditional throw lets the optimizer prove construction
// never completes, which flags the code after the placement-new (and
// after the BOOST_TEST_THROWS expression) as unreachable (MSVC /O2
// C4702). Gating the throw on a volatile read forces a re-read the
// optimizer cannot fold, so the call is not provably non-returning,
// while at runtime the throw always fires.
inline void
throw_test_exception_opaque(char const* msg)
{
    volatile bool always = true;
    if(always)
        throw test_exception(msg);
}

//----------------------------------------------------------
// Common Test Task Helpers
//----------------------------------------------------------

/** Returns a task that completes with an int value. */
inline task<int>
returns_int(int value)
{
    co_return value;
}

/** Returns a task that completes with a string value. */
inline task<std::string>
returns_string(std::string value)
{
    co_return value;
}

/** Returns a task that completes with void. */
inline task<void>
void_task()
{
    co_return;
}

/** Returns a task that throws an exception. */
inline task<int>
throws_exception(char const* msg)
{
    throw_test_exception(msg);
    co_return 0;
}

/** Returns a void task that throws an exception. */
inline task<void>
void_throws_exception(char const* msg)
{
    throw_test_exception(msg);
    co_return;
}

//----------------------------------------------------------
// Queuing Executor
//----------------------------------------------------------

/** A sized input range whose iterator holds the element it yields.

    Input iterators may do this: each increment replaces the element,
    so a reference to one does not outlive the next step.
*/
template<class Make>
class stashing_range
{
    Make make_;
    std::size_t n_;

public:
    class iterator
    {
    public:
        using value_type =
            std::invoke_result_t<Make const&, std::size_t>;
        using difference_type = std::ptrdiff_t;

    private:
        Make const* make_ = nullptr;
        std::size_t i_ = 0;
        std::size_t n_ = 0;
        mutable std::optional<value_type> stash_;

        void fill()
        {
            stash_.reset();
            if(i_ < n_)
                stash_.emplace((*make_)(i_));
        }

    public:
        iterator(Make const* make, std::size_t i, std::size_t n)
            : make_(make)
            , i_(i)
            , n_(n)
        {
            fill();
        }

        iterator(iterator&&) = default;
        iterator& operator=(iterator&&) = default;

        value_type& operator*() const { return *stash_; }

        iterator& operator++()
        {
            ++i_;
            fill();
            return *this;
        }

        void operator++(int) { ++*this; }

        bool operator==(std::default_sentinel_t) const noexcept
        {
            return i_ == n_;
        }
    };

    stashing_range(Make make, std::size_t n)
        : make_(std::move(make))
        , n_(n)
    {
    }

    iterator begin() const { return iterator(&make_, 0, n_); }
    std::default_sentinel_t end() const noexcept { return {}; }
    std::size_t size() const noexcept { return n_; }
};

/** Queuing executor that allows controlled interleaving of tasks.

    Unlike test_executor which runs tasks synchronously, this executor
    queues work and runs it in FIFO order when manually resumed.
    This allows tasks to observe stop requests between suspension points.
*/
struct queuing_executor
{
    std::queue<continuation>* queue_;
    test_io_context* ctx_ = nullptr;

    explicit queuing_executor(std::queue<continuation>& q)
        : queue_(&q)
    {
    }

    bool operator==(queuing_executor const& other) const noexcept
    {
        return queue_ == other.queue_;
    }

    test_io_context& context() const noexcept
    {
        return ctx_ ? *ctx_ : default_test_io_context();
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<> dispatch(continuation& c) const
    {
        queue_->push(c);
        return std::noop_coroutine();
    }

    void post(continuation& c) const
    {
        queue_->push(c);
    }
};

static_assert(Executor<queuing_executor>);

/** Executor that queues posts until closed, then abandons them.

    Models a context that has shut down: whatever is posted to it
    afterwards is given up rather than run.
*/
struct closing_executor
{
    std::queue<continuation>* q;
    bool* closed;

    bool operator==(closing_executor const& o) const noexcept
    {
        return q == o.q;
    }

    test_io_context& context() const noexcept
    {
        return default_test_io_context();
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<> dispatch(continuation& c) const
    {
        return c.h;
    }

    void post(continuation& c) const
    {
        if(*closed)
            capy::abandon(c);
        else
            q->push(c);
    }
};

static_assert(Executor<closing_executor>);

//----------------------------------------------------------
// Yield Awaitable
//----------------------------------------------------------

/** Awaitable that yields to the executor, allowing other tasks to run.

    When awaited, this suspends the current coroutine and posts it back
    to the executor's queue. This creates a yield point where the task
    can be interleaved with other tasks.
*/
struct yield_awaitable
{
    continuation cont_;

    bool await_ready() const noexcept
    {
        return false;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h, io_env const* env)
    {
        // Post ourselves back to the queue
        cont_.h = h;
        env->executor.post(cont_);
        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
    }
};

/** Counts its own destruction; a frame parameter of this type
    reports when the frame is destroyed.
*/
struct destroy_count_t
{
    int* n;
    explicit destroy_count_t(int& c) : n(&c) {}
    destroy_count_t(destroy_count_t&& o) noexcept
        : n(std::exchange(o.n, nullptr)) {}
    ~destroy_count_t()
    {
        if(n)
            ++*n;
    }
};

/** A suspended coroutine as its holder keeps it.

    Its handle, to resume it, and its segment's owner, to
    @ref abandon it.
*/
struct parked : continuation
{
    /// Return true while a coroutine is parked here.
    explicit operator bool() const noexcept
    {
        return static_cast<bool>(h);
    }

    /// Resume the parked coroutine.
    void resume() const
    {
        h.resume();
    }
};

/** Parks the awaiting coroutine, handing it to the test.

    The test then plays the holder: resume `*slot`, or abandon it as a
    context's teardown would.
*/
struct park_awaitable
{
    parked* slot;

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, io_env const* env) noexcept
    {
        slot->h = h;
        slot->owner = env->owner;
        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

/// Counts of a @ref counting_allocator and its copies.
struct alloc_counts
{
    std::atomic<int> allocs{0};
    std::atomic<int> deallocs{0};
    std::atomic<int> live{0};
    std::atomic<int> late{0};

    /// Return true once every allocation is freed, none late.
    bool balanced() const noexcept
    {
        return allocs.load() == deallocs.load() && late.load() == 0;
    }
};

/** An allocator whose copies count allocations and frees.

    A free made once every copy is destroyed went through an allocator
    that no longer exists, as when a frame outlives the frame holding
    its chain's allocator; it is counted as late.
*/
template<class T>
struct counting_allocator
{
    using value_type = T;

    alloc_counts* c;

    ~counting_allocator()
    {
        --c->live;
    }

    explicit counting_allocator(alloc_counts& counts) noexcept
        : c(&counts)
    {
        ++c->live;
    }

    counting_allocator(counting_allocator const& o) noexcept
        : c(o.c)
    {
        ++c->live;
    }

    template<class U>
    counting_allocator(counting_allocator<U> const& o) noexcept
        : c(o.c)
    {
        ++c->live;
    }

    counting_allocator& operator=(counting_allocator const&) = default;

    T* allocate(std::size_t n)
    {
        ++c->allocs;
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t) noexcept
    {
        ++c->deallocs;
        if(c->live.load() == 0)
            ++c->late;
        ::operator delete(p);
    }

    template<class U>
    bool operator==(counting_allocator<U> const& o) const noexcept
    {
        return c == o.c;
    }
};

/** A memory resource that counts frees made after its lease ended.

    A frame that owns a chain's allocator must outlive every frame
    allocated from it. A @ref lease held in that frame ends when the
    frame's locals are destroyed; a free counted as late came after.
*/
struct lease_resource : std::pmr::memory_resource
{
    std::atomic<bool> leased{true};
    std::atomic<int> allocs{0};
    std::atomic<int> deallocs{0};
    std::atomic<int> late{0};

    /// Return true once every allocation is freed, none late.
    bool balanced() const noexcept
    {
        return allocs.load() == deallocs.load() && late.load() == 0;
    }

protected:
    // Forwarded through another resource, which the optimizer cannot see
    // into, so it does not pair the frame's operator delete with the
    // global operator new.
    void* do_allocate(std::size_t n, std::size_t a) override
    {
        ++allocs;
        return std::pmr::new_delete_resource()->allocate(n, a);
    }

    void do_deallocate(void* p, std::size_t n, std::size_t a) override
    {
        ++deallocs;
        if(!leased.load())
            ++late;
        std::pmr::new_delete_resource()->deallocate(p, n, a);
    }

    bool do_is_equal(memory_resource const& o) const noexcept override
    {
        return this == &o;
    }
};

/// Ends the lease on a @ref lease_resource when destroyed.
struct lease
{
    lease_resource* r;

    explicit lease(lease_resource& res) noexcept
        : r(&res)
    {
    }

    lease(lease&& o) noexcept
        : r(std::exchange(o.r, nullptr))
    {
    }

    lease& operator=(lease&&) = delete;

    ~lease()
    {
        if(r)
            r->leased.store(false);
    }
};

/// Resume queued continuations in FIFO order until the queue is empty.
inline void
pump(std::queue<continuation>& q)
{
    while(!q.empty())
    {
        auto c = q.front();
        q.pop();
        safe_resume(c.h);
    }
}

} // capy
} // boost

#endif
