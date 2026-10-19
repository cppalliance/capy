//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4e.cancellation.adoc.

#include "../doc_warnings.hpp"

// GCC false positive at -O2/-O3: read_op::awaiter lives in a coroutine
// frame, and GCC cannot prove that std::optional<std::stop_callback>
// is only destroyed after emplace() has initialized it.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <boost/capy/cond.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_waker.hpp>
#include <boost/capy/ex/await_context.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_any.hpp>

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <iostream>
#include <optional>
// tag::include_stop_token[]
#include <stop_token>
// end::include_stop_token[]
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {


void do_work() {}

// tag::naive_flag[]
std::atomic<bool> should_cancel{false};

void worker()
{
    while (!should_cancel)
    {
        do_work();
    }
}
// end::naive_flag[]

// The #include directives inside the tag expand to nothing here (the
// headers are already included above); they are kept for the page text.
// tag::observer_pattern[]
#include <stop_token>
#include <iostream>

void example()
{
    std::stop_source source;

    // Create tokens (distribute notification capability)
    std::stop_token token1 = source.get_token();
    std::stop_token token2 = source.get_token();  // Same underlying state

    // Register callbacks (observers)
    std::stop_callback cb1(token1, []{ std::cout << "Observer 1 notified\n"; });
    std::stop_callback cb2(token2, []{ std::cout << "Observer 2 notified\n"; });

    std::cout << "Before signal\n";
    source.request_stop();  // Triggers all callbacks
    std::cout << "After signal\n";
}
// end::observer_pattern[]

capy::task<> child();

// tag::token_propagation[]
capy::task<> parent()
{
    // Our stop token is automatically passed to child
    co_await child();
}

capy::task<> child()
{
    // Receives parent's stop token via ContextAwaitable protocol
    auto token = co_await capy::this_coro::stop_token;  // Access current token
}
// end::token_propagation[]

capy::task<> do_chunk_of_work() { co_return; }

// tag::access_stop_token[]
capy::task<> cancellable_work()
{
    auto token = co_await capy::this_coro::stop_token;

    while (!token.stop_requested())
    {
        co_await do_chunk_of_work();
    }
}
// end::access_stop_token[]

struct Item {};

capy::task<> process(Item const&) { co_return; }

// tag::check_token[]
capy::task<> process_items(std::vector<Item> const& items)
{
    auto token = co_await capy::this_coro::stop_token;

    for (auto const& item : items)
    {
        if (token.stop_requested())
            co_return;  // Exit early

        co_await process(item);
    }
}
// end::check_token[]

struct resource_handle {};

resource_handle acquire_resource() { return {}; }

capy::task<> use_resource(resource_handle&) { co_return; }

// tag::raii_cleanup[]
capy::task<> with_resource()
{
    auto resource = acquire_resource();  // RAII wrapper
    auto token = co_await capy::this_coro::stop_token;

    while (!token.stop_requested())
    {
        co_await use_resource(resource);
    }
    // resource destructor runs regardless of how we exit
}
// end::raii_cleanup[]

capy::task<std::string> do_fetch() { co_return "payload"; }

// tag::canceled_convention[]
capy::task<std::string> fetch_with_cancel()
{
    auto token = co_await capy::this_coro::stop_token;

    if (token.stop_requested())
    {
        throw std::system_error(
            capy::make_error_code(capy::error::canceled));
    }

    co_return co_await do_fetch();
}
// end::canceled_convention[]

// Compile-only bug demo: running it would resume a coroutine twice, so
// nothing awaits it. start_read stands in for the I/O backend.
struct racy_read;
void start_read(racy_read*) {}

// tag::post_without_claim[]
// WRONG: the completion and the stop callback can both resume the
// coroutine.
struct racy_read
{
    struct cancel_fn
    {
        racy_read* self;

        void operator()() const noexcept
        {
            self->canceled_ = true;
            self->ex_.post(self->cont_);  // even if the read completed
        }
    };

    capy::continuation cont_;
    capy::executor_ref ex_;
    bool canceled_ = false;
    std::optional<std::stop_callback<cancel_fn>> stop_cb_;

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h, capy::await_context const* env)
    {
        cont_.h = h;
        cont_.owner = env->owner;
        ex_ = env->executor;
        stop_cb_.emplace(env->stop_token, cancel_fn{this});
        start_read(this);  // the backend calls on_complete() later
        return std::noop_coroutine();
    }

    // Called by the I/O backend, on any thread, when the read finishes.
    void on_complete() noexcept
    {
        ex_.post(cont_);  // even if the stop callback posted already
    }

    void await_resume() noexcept { stop_cb_.reset(); }
};
// end::post_without_claim[]

// tag::stoppable_awaitable[]
// An I/O object with one pending read. The I/O backend calls complete()
// from any thread when the read finishes.
class read_op
{
public:
    struct awaiter;

    awaiter wait() noexcept;

    // Called by the I/O backend. Ignored if no wait is armed, or if a
    // stop request already claimed the wait.
    void complete(std::size_t n) noexcept;

    bool waiting() const noexcept
    {
        return armed_.load(std::memory_order_acquire);
    }

private:
    // The claim: whoever moves armed_ from true to false owns the
    // result. The loser does not touch the waiter.
    bool claim() noexcept
    {
        bool expected = true;
        return armed_.compare_exchange_strong(
            expected, false, std::memory_order_acq_rel);
    }

    std::atomic<bool> armed_{false};
    awaiter* waiter_ = nullptr;
};

struct read_op::awaiter
{
    // Runs on the thread that calls request_stop().
    struct cancel_fn
    {
        awaiter* self;

        void operator()() const noexcept
        {
            if (!self->op_->claim())
                return;  // complete() won, or nothing is armed yet
            self->canceled_ = true;
            self->arrive();
        }
    };

    read_op* op_;
    capy::continuation cont_;
    capy::executor_ref ex_;
    std::size_t bytes_ = 0;
    bool canceled_ = false;
    // The winner and the end of await_suspend both arrive here. The
    // second to arrive resumes the coroutine, so it cannot resume while
    // await_suspend is still running.
    std::atomic<int> arrivals_{0};
    std::optional<std::stop_callback<cancel_fn>> stop_cb_;

    explicit awaiter(read_op* op) noexcept : op_(op) {}

    // co_await can move the awaiter, but only before await_suspend,
    // when there is no stop callback yet.
    awaiter(awaiter&& other) noexcept : op_(other.op_) {}

    // Called by the winner. If await_suspend has returned, post the
    // coroutine; otherwise await_suspend resumes it.
    void arrive() noexcept
    {
        if (arrivals_.fetch_add(1, std::memory_order_acq_rel) == 1)
            ex_.post(cont_);  // last action: *this can be destroyed now
    }

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h, capy::await_context const* env)
    {
        if (env->stop_token.stop_requested())
        {
            canceled_ = true;
            return h;
        }

        // 1. Write everything the winner reads, before anyone can win.
        //    The owner lets a holder abandon the chain while it waits.
        cont_.h = h;
        cont_.owner = env->owner;
        ex_ = env->executor;
        op_->waiter_ = this;

        // 2. If a stop is requested now, the callback runs here and
        //    loses, because nothing is armed yet. Step 4 catches it.
        stop_cb_.emplace(env->stop_token, cancel_fn{this});

        // 3. Arm. complete() or the stop callback can win from here on.
        op_->armed_.store(true, std::memory_order_release);

        // 4. Claim a stop request that arrived during step 2.
        if (env->stop_token.stop_requested() && op_->claim())
        {
            canceled_ = true;
            return h;
        }

        // 5. Arrive. If the winner came first, resume now, on this
        //    thread. Otherwise the winner posts the coroutine later.
        if (arrivals_.fetch_add(1, std::memory_order_acq_rel) == 1)
            return h;
        return std::noop_coroutine();
    }

    capy::results<std::size_t> await_resume() noexcept
    {
        // Waits for a stop callback that is still running to return.
        stop_cb_.reset();
        if (canceled_)
            return {capy::make_error_code(capy::error::canceled), 0};
        return {std::error_code(), bytes_};
    }
};

inline read_op::awaiter read_op::wait() noexcept
{
    return awaiter{this};
}

inline void read_op::complete(std::size_t n) noexcept
{
    if (!claim())
        return;
    waiter_->bytes_ = n;
    waiter_->arrive();
}
// end::stoppable_awaitable[]

capy::task<> read_once(
    read_op& op, std::atomic<int>& resumed, std::atomic<int>& canceled)
{
    auto [ec, n] = co_await op.wait();
    if (ec == capy::cond::canceled)
        canceled.fetch_add(1);
    else if (!ec && n == 5)
        resumed.fetch_add(1);
}

// tag::racing_deadline[]
struct fetch_channel
{
    capy::async_waker fetch_ready;
    std::atomic<bool> cancelled{false};
    std::string result;
};

// One side of the race: completes when the fetch worker thread
// wakes fetch_ready. If the deadline wins first, when_any's stop
// request cancels the wait; flag the worker so it stops early.
capy::task<capy::results<std::string>> await_fetch(fetch_channel& ch)
{
    auto [ec] = co_await ch.fetch_ready.wait();
    if (ec)
    {
        ch.cancelled.store(true);
        co_return capy::results<std::string>{ec, {}};
    }
    co_return capy::results<std::string>{std::error_code(), std::move(ch.result)};
}

// The other side of the race: completes once whatever plays the
// clock wakes the waker.
capy::task<capy::results<>> deadline(capy::async_waker& waker)
{
    auto [ec] = co_await waker.wait();
    co_return capy::results<>{ec};
}
// end::racing_deadline[]

capy::task<> race(
    fetch_channel& ch, capy::async_waker& waker, std::size_t& winner)
{
    auto result = co_await capy::when_any(await_fetch(ch), deadline(waker));
    winner = result.index();
}

capy::task<> fetch_next_chunk(std::string const&) { co_return; }

capy::task<void> download(std::string url);

// tag::user_cancellation[]
class download_manager
{
    capy::executor_ref executor_;
    std::stop_source stop_source_;

public:
    void start_download(std::string url)
    {
        // Token propagated via await_context, not as a function argument
        capy::run_async(executor_, stop_source_.get_token())(download(url));
    }

    void cancel()
    {
        stop_source_.request_stop();
    }
};

capy::task<void> download(std::string url)
{
    // From run_async's await_context
    auto token = co_await capy::this_coro::stop_token;
    while (!token.stop_requested())
    {
        co_await fetch_next_chunk(url);
    }
}
// end::user_cancellation[]

// download_manager needs a live execution context to launch; the
// class itself is the demonstration, so nothing instantiates it.
[[maybe_unused]] capy::task<void> (* const download_demo)(std::string)
    = &download;

struct connection {};

capy::task<> process_request(connection&) { co_return; }

capy::task<> send_goodbye(connection&) { co_return; }

// tag::graceful_shutdown[]
class server
{
    std::stop_source shutdown_source_;

public:
    void shutdown()
    {
        shutdown_source_.request_stop();
        // All pending operations receive stop request
    }

    capy::task<> handle_connection(connection conn)
    {
        auto token = shutdown_source_.get_token();

        while (!token.stop_requested())
        {
            co_await process_request(conn);
        }

        // Graceful cleanup
        co_await send_goodbye(conn);
    }
};
// end::graceful_shutdown[]

struct cancellation_test
{
    void
    testNaiveFlag()
    {
        should_cancel = true;
        worker();
        BOOST_TEST(should_cancel.load());
    }

    void
    testObserverPattern()
    {
        example();
    }

    void
    testImmediateInvocation()
    {
        // tag::immediate_invocation[]
        std::stop_source source;
        source.request_stop();  // Already signaled

        // Callback runs in constructor, not later
        std::stop_callback cb(source.get_token(), []{
            std::cout << "Runs immediately!\n";
        });
        // end::immediate_invocation[]
        BOOST_TEST(source.stop_requested());
    }

    void
    testResetWorkaround()
    {
        // tag::reset_workaround[]
        std::stop_source source;
        auto token = source.get_token();

        // ... distribute token to workers ...

        source.request_stop();  // Triggered, now permanently signaled

        // To "reset": create new source
        source = std::stop_source{};  // New shared state
        // Old tokens still see the old, already-signaled state

        // Must redistribute new tokens to ALL holders of the old token
        auto new_token = source.get_token();
        // end::reset_workaround[]
        BOOST_TEST(token.stop_requested());
        BOOST_TEST(!new_token.stop_requested());
    }

    void
    testTokenPropagation()
    {
        capy::thread_pool pool(1);
        capy::run_async(pool.get_executor())(parent());
        pool.join();
    }

    void
    testAccessStopToken()
    {
        capy::thread_pool pool(1);
        std::stop_source source;
        source.request_stop();
        capy::run_async(
            pool.get_executor(), source.get_token())(cancellable_work());
        pool.join();
    }

    void
    testCheckToken()
    {
        capy::thread_pool pool(1);
        std::vector<Item> items(3);
        capy::run_async(pool.get_executor())(process_items(items));
        pool.join();
    }

    void
    testRaiiCleanup()
    {
        capy::thread_pool pool(1);
        std::stop_source source;
        source.request_stop();
        capy::run_async(
            pool.get_executor(), source.get_token())(with_resource());
        pool.join();
    }

    void
    testCanceledConvention()
    {
        capy::thread_pool pool(1);
        std::stop_source source;
        source.request_stop();
        bool canceled = false;
        capy::run_async(pool.get_executor(), source.get_token(),
            [](std::string const&) {},
            [&canceled](std::exception_ptr ep)
            {
                try
                {
                    std::rethrow_exception(ep);
                }
                catch (std::system_error const& e)
                {
                    canceled = e.code() == capy::cond::canceled;
                }
            })(fetch_with_cancel());
        pool.join();
        BOOST_TEST(canceled);
    }

    void
    testStoppableAwaitable()
    {
        // Declared before the pool, so they outlive its work.
        std::atomic<int> resumed{0};
        std::atomic<int> canceled{0};
        read_op stopped_op;
        std::stop_source stopped;
        int const rounds = 500;

        capy::thread_pool pool(2);

        // Stop requested before the wait: the fast path.
        stopped.request_stop();
        capy::run_async(pool.get_executor(), stopped.get_token())(
            read_once(stopped_op, resumed, canceled));

        // complete() racing request_stop(). Exactly one of them wins
        // each round, so every round resumes the task exactly once.
        for (int i = 0; i < rounds; ++i)
        {
            read_op op;
            std::stop_source source;
            std::atomic<bool> done{false};
            // The backend completes the read the moment the wait is
            // armed, which can be before await_suspend returns.
            std::thread backend([&op, &done] {
                while (!op.waiting() && !done.load())
                    std::this_thread::yield();
                op.complete(5);  // a no-op if the stop request won
            });
            capy::run_async(pool.get_executor(), source.get_token(),
                [&done] { done.store(true); })(
                    read_once(op, resumed, canceled));
            while (!op.waiting() && !done.load())
                std::this_thread::yield();
            source.request_stop();
            backend.join();
            while (!done.load())
                std::this_thread::yield();
        }

        pool.join();
        BOOST_TEST_EQ(resumed.load() + canceled.load(), rounds + 1);
        BOOST_TEST(canceled.load() >= 1);
    }

    void
    testRacingDeadline()
    {
        capy::thread_pool pool(1);
        fetch_channel ch;
        capy::async_waker deadline_waker;
        std::size_t winner = 0;
        capy::run_async(
            pool.get_executor())(race(ch, deadline_waker, winner));
        // The test thread plays the clock: an early wake is latched,
        // so ordering against the race's suspension is benign.
        deadline_waker.wake();
        pool.join();
        BOOST_TEST(winner == 2);
        BOOST_TEST(ch.cancelled.load());
    }

    void
    testGracefulShutdown()
    {
        capy::thread_pool pool(1);
        server s;
        s.shutdown();
        capy::run_async(pool.get_executor())(s.handle_connection({}));
        pool.join();
    }

    void
    run()
    {
        testNaiveFlag();
        testObserverPattern();
        testImmediateInvocation();
        testResetWorkaround();
        testTokenPropagation();
        testAccessStopToken();
        testCheckToken();
        testRaiiCleanup();
        testCanceledConvention();
        testStoppableAwaitable();
        testRacingDeadline();
        testGracefulShutdown();
    }
};

} // namespace

TEST_SUITE(cancellation_test, "boost.capy.doc.4e_cancellation");
