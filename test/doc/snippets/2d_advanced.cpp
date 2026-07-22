//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/2.cpp20-coroutines/2d.advanced.adoc.
// Pages include the tagged regions; scaffolding stays outside the tags.

// Fragments deliberately leave results and bindings unused; the pages
// explain the values in prose instead.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-function"
// gcc 15 with sanitizers misattributes coroutine frame delete paths
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
#if defined(_MSC_VER)
#pragma warning(disable: 4834) // discarding [[nodiscard]] return value
#pragma warning(disable: 4189) // local variable initialized but not referenced
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4101) // unreferenced local variable
#pragma warning(disable: 4456) // declaration hides previous local declaration
#pragma warning(disable: 4457) // declaration hides function parameter
#pragma warning(disable: 4458) // declaration hides class member
#pragma warning(disable: 4459) // declaration hides global declaration
#endif

#include <boost/capy/task.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/thread_pool.hpp>

#include <cstddef>
#include <new>

#include "test_suite.hpp"

// The production generator's tagged region carries its own includes, so
// it lives at file scope; the page shows it as a standalone listing.

// tag::production_generator[]
#include <coroutine>
#include <exception>
#include <iterator>
#include <utility>

template<typename T>
class Generator
{
public:
    struct promise_type
    {
        T value_;
        std::exception_ptr exception_;

        Generator get_return_object()
        {
            return Generator{Handle::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(T v)
        {
            value_ = std::move(v);
            return {};
        }

        void return_void() noexcept {}

        void unhandled_exception()
        {
            exception_ = std::current_exception();
        }

        // Prevent co_await inside generators
        template<typename U>
        std::suspend_never await_transform(U&&) = delete;
    };

    using Handle = std::coroutine_handle<promise_type>;

    class iterator
    {
        Handle handle_;

    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;

        iterator() : handle_(nullptr) {}
        explicit iterator(Handle h) : handle_(h) {}

        iterator& operator++()
        {
            handle_.resume();
            if (handle_.done())
            {
                auto& promise = handle_.promise();
                handle_ = nullptr;
                if (promise.exception_)
                    std::rethrow_exception(promise.exception_);
            }
            return *this;
        }

        T& operator*() const { return handle_.promise().value_; }
        bool operator==(iterator const& other) const
        {
            return handle_ == other.handle_;
        }
    };

    iterator begin()
    {
        if (handle_)
        {
            handle_.resume();
            if (handle_.done())
            {
                auto& promise = handle_.promise();
                if (promise.exception_)
                    std::rethrow_exception(promise.exception_);
                return iterator{};
            }
        }
        return iterator{handle_};
    }

    iterator end() { return iterator{}; }

    ~Generator() { if (handle_) handle_.destroy(); }

    Generator(Generator const&) = delete;
    Generator& operator=(Generator const&) = delete;

    Generator(Generator&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}

    Generator& operator=(Generator&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

private:
    Handle handle_;

    explicit Generator(Handle h) : handle_(h) {}
};
// end::production_generator[]

namespace capy = boost::capy;

namespace {

using capy::task;

task<> b();
task<> c();

// tag::task_chain[]
task<> a() { co_await b(); }
task<> b() { co_await c(); }
task<> c() { co_return; }
// end::task_chain[]

// Awaiter scaffolding for the symmetric-transfer fragment
struct chained_awaiter
{
    std::coroutine_handle<> continuation_;
    std::coroutine_handle<> next_coroutine_;

    bool await_ready() const noexcept { return false; }

    // tag::symmetric_await_suspend[]
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h)
    {
        // store continuation for later
        continuation_ = h;

        // return handle to resume (instead of calling resume())
        return next_coroutine_;
    }
    // end::symmetric_await_suspend[]

    void await_resume() const noexcept {}
};

// A promise sketch large enough for the final_suspend fragment
namespace symmetric_generator {

struct promise_type
{
    std::coroutine_handle<> consumer_handle_;

    // tag::generator_final_suspend[]
    auto final_suspend() noexcept
    {
        struct awaiter
        {
            promise_type* p_;

            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept
            {
                // Return to the consumer that called resume()
                return p_->consumer_handle_;
            }

            void await_resume() const noexcept {}
        };
        return awaiter{this};
    }
    // end::generator_final_suspend[]
};

} // namespace symmetric_generator

task<void> compute_something()
{
    co_return;
}

void store_for_later(task<void>) {}

[[maybe_unused]] task<void> halo_demo()
{
    // tag::halo[]
    // HALO might apply here because the task is awaited immediately
    co_await compute_something();

    // HALO cannot apply here because the task escapes
    auto task = compute_something();
    store_for_later(std::move(task));
    // end::halo[]
}

namespace custom_alloc {

// Stand-in allocator so the fragment's calls resolve
struct frame_allocator
{
    void* allocate(std::size_t n) { return ::operator new(n); }
    void deallocate(void* p, std::size_t) { ::operator delete(p); }
};

frame_allocator my_allocator;

// tag::custom_operator_new[]
struct promise_type
{
    // Custom allocation
    static void* operator new(std::size_t size)
    {
        return my_allocator.allocate(size);
    }

    static void operator delete(void* ptr, std::size_t size)
    {
        my_allocator.deallocate(ptr, size);
    }

    // ... rest of promise type
};
// end::custom_operator_new[]

} // namespace custom_alloc

// Promise sketches for the unhandled_exception policy fragments
struct terminate_promise
{
    // tag::unhandled_terminate[]
    void unhandled_exception()
    {
        std::terminate();
    }
    // end::unhandled_terminate[]
};

struct store_promise
{
    std::exception_ptr exception_;

    // tag::unhandled_store[]
    void unhandled_exception()
    {
        exception_ = std::current_exception();
    }
    // end::unhandled_store[]
};

struct rethrow_promise
{
    // tag::unhandled_rethrow[]
    void unhandled_exception()
    {
        throw;  // propagates to whoever resumed the coroutine
    }
    // end::unhandled_rethrow[]
};

struct swallow_promise
{
    // tag::unhandled_swallow[]
    void unhandled_exception()
    {
        // silently ignored - almost always a mistake
    }
    // end::unhandled_swallow[]
};

namespace store_rethrow {

// Stub handle so the accessor sketch compiles without a real coroutine
template<class T>
struct stub_promise
{
    std::exception_ptr exception_;
    T result_;
};

template<class T>
struct stub_handle
{
    stub_promise<T>* p_ = nullptr;
    stub_promise<T>& promise() const { return *p_; }
};

template<class T>
class return_object
{
    stub_handle<T> handle_;

public:
    // tag::store_and_rethrow[]
    struct promise_type
    {
        std::exception_ptr exception_;

        void unhandled_exception()
        {
            exception_ = std::current_exception();
        }
    };

    // In the return object's result accessor:
    T get_result()
    {
        if (handle_.promise().exception_)
            std::rethrow_exception(handle_.promise().exception_);
        return std::move(handle_.promise().result_);
    }
    // end::store_and_rethrow[]
};

template class return_object<int>;

} // namespace store_rethrow

Generator<int> iota(int n)
{
    for (int i = 0; i < n; ++i)
        co_yield i;
}

struct advanced_test
{
    void testTaskChain()
    {
        capy::thread_pool pool(1);
        capy::run_async(pool.get_executor())(a());
        pool.join();
    }

    void testProductionGenerator()
    {
        int sum = 0;
        for (int v : iota(5))
            sum += v;
        BOOST_TEST(sum == 10);
    }

    void run()
    {
        testTaskChain();
        testProductionGenerator();
    }
};

} // namespace

TEST_SUITE(advanced_test, "boost.capy.doc.2d_advanced");
