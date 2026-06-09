//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Cover the owning-construction rollback in the any_* type-erased
// wrappers: when the cached-awaitable allocation fails after the
// wrapped object is already constructed, the guard must destroy that
// object and free its storage. The path is only reachable when
// operator new throws, so this translation unit installs an
// instrumented global operator new with a per-thread fail toggle.
//
// Defined here (once per test binary) to avoid an ODR clash with the
// other any_* test files in the combined CMake executable.
//
// Replacing the global operator new collides with the sanitizer
// runtimes, which provide their own (strong) operator new/delete under
// static linking. The instrumented path is therefore compiled out when
// an allocation-instrumenting sanitizer is active; the OOM-rollback line
// is covered by the (non-sanitizer) coverage build.

#include <boost/capy/io/any_read_stream.hpp>
#include <boost/capy/io/any_write_stream.hpp>
#include <boost/capy/io/any_read_source.hpp>
#include <boost/capy/io/any_buffer_source.hpp>
#include <boost/capy/io/any_write_sink.hpp>
#include <boost/capy/io/any_buffer_sink.hpp>
#include <boost/capy/io/any_stream.hpp>

#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>

#include "test_suite.hpp"

#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <span>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
# define CAPY_OOM_INSTRUMENTED_NEW 0
#elif defined(__has_feature)
# if __has_feature(address_sanitizer) || \
     __has_feature(thread_sanitizer) || \
     __has_feature(memory_sanitizer)
#  define CAPY_OOM_INSTRUMENTED_NEW 0
# else
#  define CAPY_OOM_INSTRUMENTED_NEW 1
# endif
#else
# define CAPY_OOM_INSTRUMENTED_NEW 1
#endif

#if CAPY_OOM_INSTRUMENTED_NEW

namespace {

// When armed, the next allocation at least this large throws bad_alloc,
// then disarms. Per-thread so unrelated work on other threads is never
// affected. malloc/free keep AddressSanitizer's tracking intact.
thread_local std::size_t fail_alloc_threshold = 0;

void arm_alloc_failure(std::size_t threshold) noexcept
{
    fail_alloc_threshold = threshold;
}

void disarm_alloc_failure() noexcept
{
    fail_alloc_threshold = 0;
}

} // namespace

#if defined(_MSC_VER)
# define OOM_NOINLINE __declspec(noinline)
#else
# define OOM_NOINLINE __attribute__((noinline))
#endif

// noinline so the compiler sees paired operator new / operator delete
// calls rather than inlined malloc / free, which trips GCC's
// -Wmismatched-new-delete heuristic at the wrapper's rollback site.

OOM_NOINLINE void* operator new(std::size_t n)
{
    if(fail_alloc_threshold && n >= fail_alloc_threshold)
    {
        fail_alloc_threshold = 0;
        throw std::bad_alloc();
    }
    if(void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}

OOM_NOINLINE void* operator new[](std::size_t n)
{
    return ::operator new(n);
}

OOM_NOINLINE void* operator new(std::size_t n, std::nothrow_t const&) noexcept
{
    return std::malloc(n ? n : 1);
}

OOM_NOINLINE void* operator new[](std::size_t n, std::nothrow_t const&) noexcept
{
    return std::malloc(n ? n : 1);
}

OOM_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
OOM_NOINLINE void operator delete[](void* p) noexcept { std::free(p); }
OOM_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
OOM_NOINLINE void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
OOM_NOINLINE void operator delete(void* p, std::nothrow_t const&) noexcept { std::free(p); }
OOM_NOINLINE void operator delete[](void* p, std::nothrow_t const&) noexcept { std::free(p); }

#endif // CAPY_OOM_INSTRUMENTED_NEW

namespace boost {
namespace capy {
namespace {

#if CAPY_OOM_INSTRUMENTED_NEW

constexpr std::size_t big_awaitable = 64 * 1024;
constexpr std::size_t fail_threshold = 32 * 1024;

// A large awaitable so its cached-storage allocation dominates and is
// the one targeted by the fail threshold. It is never constructed (the
// allocation throws first), only sized.
template<class Result>
struct oom_awaitable
{
    unsigned char pad_[big_awaitable];
    bool await_ready() const noexcept { return true; }
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h, io_env const*) noexcept
    {
        return h;
    }
    Result await_resume() { return {}; }
};

// Counts destructions so the test can confirm the rollback ran. A
// user-declared copy ctor and destructor suppress the move ctor, so the
// wrapper copies the value and both the parameter and the stored copy
// increment the counter.
struct oom_counter
{
    int* destroyed_;
    explicit oom_counter(int* d) noexcept : destroyed_(d) {}
    oom_counter(oom_counter const&) noexcept = default;
    ~oom_counter() { if(destroyed_) ++(*destroyed_); }
};

struct oom_read_stream : oom_counter
{
    using oom_counter::oom_counter;
    oom_awaitable<io_result<std::size_t>> read_some(MutableBufferSequence auto)
        { return {}; }
};

struct oom_write_stream : oom_counter
{
    using oom_counter::oom_counter;
    oom_awaitable<io_result<std::size_t>> write_some(ConstBufferSequence auto)
        { return {}; }
};

struct oom_read_source : oom_counter
{
    using oom_counter::oom_counter;
    oom_awaitable<io_result<std::size_t>> read_some(MutableBufferSequence auto)
        { return {}; }
    oom_awaitable<io_result<std::size_t>> read(MutableBufferSequence auto)
        { return {}; }
};

struct oom_buffer_source : oom_counter
{
    using oom_counter::oom_counter;
    oom_awaitable<io_result<std::span<const_buffer>>>
    pull(std::span<const_buffer>) { return {}; }
    void consume(std::size_t) noexcept {}
    oom_awaitable<io_result<std::size_t>> read_some(MutableBufferSequence auto)
        { return {}; }
    oom_awaitable<io_result<std::size_t>> read(MutableBufferSequence auto)
        { return {}; }
};

struct oom_write_sink : oom_counter
{
    using oom_counter::oom_counter;
    oom_awaitable<io_result<std::size_t>> write_some(ConstBufferSequence auto)
        { return {}; }
    oom_awaitable<io_result<std::size_t>> write(ConstBufferSequence auto)
        { return {}; }
    oom_awaitable<io_result<std::size_t>> write_eof(ConstBufferSequence auto)
        { return {}; }
    oom_awaitable<io_result<>> write_eof() { return {}; }
};

struct oom_buffer_sink : oom_counter
{
    using oom_counter::oom_counter;
    std::span<mutable_buffer> prepare(std::span<mutable_buffer>) { return {}; }
    oom_awaitable<io_result<>> commit(std::size_t) { return {}; }
    oom_awaitable<io_result<>> commit_eof(std::size_t) { return {}; }
    oom_awaitable<io_result<std::size_t>> write_some(ConstBufferSequence auto)
        { return {}; }
    oom_awaitable<io_result<std::size_t>> write(ConstBufferSequence auto)
        { return {}; }
    oom_awaitable<io_result<std::size_t>> write_eof(ConstBufferSequence auto)
        { return {}; }
    oom_awaitable<io_result<>> write_eof() { return {}; }
};

struct oom_stream : oom_counter
{
    using oom_counter::oom_counter;
    oom_awaitable<io_result<std::size_t>> read_some(MutableBufferSequence auto)
        { return {}; }
    oom_awaitable<io_result<std::size_t>> write_some(ConstBufferSequence auto)
        { return {}; }
};

#endif // CAPY_OOM_INSTRUMENTED_NEW

class any_oom_rollback_test
{
public:
#if CAPY_OOM_INSTRUMENTED_NEW
    // Runtime tools (e.g. valgrind) intercept operator new and bypass
    // the replacement below, so the forced failure never fires there.
    // Probe once: arm the toggle and confirm a large allocation actually
    // throws. If it doesn't, the instrumentation isn't in effect.
    static bool
    instrumented_new_in_effect()
    {
        arm_alloc_failure(fail_threshold);
        void* p = nullptr;
        bool threw = false;
        try { p = ::operator new(big_awaitable); }
        catch(std::bad_alloc const&) { threw = true; }
        disarm_alloc_failure();
        ::operator delete(p);  // frees the probe block if it wasn't thrown
        return threw;
    }

    template<class Wrapper, class Mock>
    void
    check()
    {
        int destroyed = 0;
        arm_alloc_failure(fail_threshold);
        BOOST_TEST_THROWS(Wrapper(Mock(&destroyed)), std::bad_alloc);
        disarm_alloc_failure();

        // The wrapped object was constructed before the cached-awaitable
        // allocation failed, so the rollback guard destroyed it.
        BOOST_TEST(destroyed >= 1);
    }
#endif

    void
    run()
    {
#if CAPY_OOM_INSTRUMENTED_NEW
        if(!instrumented_new_in_effect())
        {
            // operator new is intercepted (valgrind etc.); the rollback
            // path is covered by the normal coverage build.
            BOOST_TEST(true);
            return;
        }
        check<any_read_stream, oom_read_stream>();
        check<any_write_stream, oom_write_stream>();
        check<any_read_source, oom_read_source>();
        check<any_buffer_source, oom_buffer_source>();
        check<any_write_sink, oom_write_sink>();
        check<any_buffer_sink, oom_buffer_sink>();
        check<any_stream, oom_stream>();
#else
        // The instrumented operator new is unavailable under a
        // sanitizer; the OOM-rollback path is covered by the coverage
        // build instead.
        BOOST_TEST(true);
#endif
    }
};

TEST_SUITE(any_oom_rollback_test, "boost.capy.io.any_oom_rollback");

} // namespace
} // capy
} // boost
