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

#include <boost/capy/cond.hpp>
#include <boost/capy/continuation.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/async_waker.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_any.hpp>

#include <atomic>
#include <coroutine>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
// tag::include_stop_token[]
#include <stop_token>
// end::include_stop_token[]
#include <string>
#include <system_error>
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
    // Receives parent's stop token via IoAwaitable protocol
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

// tag::inline_resume_wrong[]
// WRONG — causes use-after-free
std::optional<std::stop_callback<std::coroutine_handle<>>> stop_cb;

std::coroutine_handle<> await_suspend(
    std::coroutine_handle<> h, capy::io_env const* env)
{
    stop_cb.emplace(env->stop_token, h);  // Resumes inline!
    return std::noop_coroutine();
}
// end::inline_resume_wrong[]

// Compile-only bug demo: firing the callback would resume the
// coroutine on the stopping thread, so nothing ever calls it.
[[maybe_unused]] std::coroutine_handle<> (* const wrong_pattern)(
    std::coroutine_handle<>, capy::io_env const*) = &await_suspend;

void start_async_operation(std::coroutine_handle<>, capy::io_env const*) {}

// tag::stoppable_awaitable[]
struct my_stoppable_awaitable
{
    using stop_cb_t = std::stop_callback<std::function<void()>>;

    mutable capy::continuation cont_;
    std::unique_ptr<stop_cb_t> stop_cb_;
    // ... other members for the async operation ...

    bool await_ready() { return false; }

    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> h, capy::io_env const* env)
    {
        if (env->stop_token.stop_requested())
            return h;  // Already cancelled

        cont_.h = h;
        auto ex = env->executor;
        stop_cb_ = std::make_unique<stop_cb_t>(env->stop_token,
            [this, ex]() mutable noexcept { ex.post(cont_); });

        start_async_operation(h, env);
        return std::noop_coroutine();
    }

    void await_resume() { /* check result or throw */ }
};
// end::stoppable_awaitable[]

capy::task<> use_stoppable()
{
    co_await my_stoppable_awaitable{};
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
capy::io_task<std::string> await_fetch(fetch_channel& ch)
{
    auto [ec] = co_await ch.fetch_ready.wait();
    if (ec)
    {
        ch.cancelled.store(true);
        co_return capy::io_result<std::string>{ec, {}};
    }
    co_return capy::io_result<std::string>{std::error_code(), std::move(ch.result)};
}

// The other side of the race: completes once whatever plays the
// clock wakes the waker.
capy::io_task<> deadline(capy::async_waker& waker)
{
    auto [ec] = co_await waker.wait();
    co_return capy::io_result<>{ec};
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
        // Token propagated via io_env, not as a function argument
        capy::run_async(executor_, stop_source_.get_token())(download(url));
    }

    void cancel()
    {
        stop_source_.request_stop();
    }
};

capy::task<void> download(std::string url)
{
    // From run_async's io_env
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
        // A pre-signaled token exercises the already-cancelled fast
        // path; the armed path would wait on an operation that this
        // sketch never starts.
        capy::thread_pool pool(1);
        std::stop_source source;
        source.request_stop();
        capy::run_async(
            pool.get_executor(), source.get_token())(use_stoppable());
        pool.join();
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
