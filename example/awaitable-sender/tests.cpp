//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "awaitable_sender.hpp"
#include "awaitable_sender_base.hpp"
#include "read_op.hpp"

#include <boost/capy.hpp>

#include <beman/execution/execution.hpp>

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <latch>
#include <stdexcept>
#include <stop_token>
#include <system_error>
#include <thread>
#include <memory_resource>
#include <tuple>
#include <type_traits>
#include <utility>

namespace capy = boost::capy;
namespace ex = beman::execution;

static int failures = 0;

#define CHECK(cond) \
    do { \
        if(!(cond)) \
        { \
            ++failures; \
            std::cerr << "FAIL " << __FILE__ << ":" \
                << __LINE__ << ": " #cond "\n"; \
        } \
    } while(0)

enum class channel { none, value, error, stopped };

struct test_outcome
{
    channel ch = channel::none;
    std::size_t n = 0;
    int i = 0;
    std::error_code ec{};
};

template<class Token>
struct test_env
{
    capy::executor_ref ex_;
    Token tok_;

    auto query(capy::get_io_executor_t const&) const noexcept
        -> capy::executor_ref
    {
        return ex_;
    }

    auto query(ex::get_stop_token_t const&) const noexcept
        -> Token
    {
        return tok_;
    }
};

template<class Token>
struct test_receiver
{
    using receiver_concept = ex::receiver_t;

    test_env<Token> env_;
    test_outcome* out_;
    std::latch* done_;

    auto get_env() const noexcept -> test_env<Token>
    {
        return env_;
    }

    void set_value() && noexcept
    {
        out_->ch = channel::value;
        done_->count_down();
    }

    void set_value(std::size_t n) && noexcept
    {
        out_->ch = channel::value;
        out_->n = n;
        done_->count_down();
    }

    void set_value(int i) && noexcept
    {
        out_->ch = channel::value;
        out_->i = i;
        done_->count_down();
    }

    void set_error(std::error_code ec) && noexcept
    {
        out_->ch = channel::error;
        out_->ec = ec;
        done_->count_down();
    }

    void set_error(std::exception_ptr) && noexcept
    {
        out_->ch = channel::error;
        done_->count_down();
    }

    void set_stopped() && noexcept
    {
        out_->ch = channel::stopped;
        done_->count_down();
    }
};

// Connect, start, and wait for any sender against a fresh
// single-thread pool; returns the observed completion.
template<class Sender, class Token = std::stop_token>
test_outcome run(Sender&& sndr, Token tok = {})
{
    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    std::latch done(1);
    test_outcome out;
    auto op = ex::connect(
        std::forward<Sender>(sndr),
        test_receiver<Token>{
            {pool_ex, tok}, &out, &done});
    ex::start(op);
    done.wait();
    return out;
}

// Poll a latch with a deadline so a cancellation regression fails
// the suite instead of hanging it. Returns false on timeout; the
// caller must then exit the process, because abandoning a pending
// operation whose state lives on its stack would be use-after-free.
bool await_latch(
    std::latch& l,
    std::chrono::milliseconds limit)
{
    auto deadline = std::chrono::steady_clock::now() + limit;
    while(!l.try_wait())
    {
        if(std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }
    return true;
}

#define WAIT_OR_DIE(latch) \
    do { \
        if(!await_latch(latch, std::chrono::seconds(2))) \
        { \
            ++failures; \
            std::cerr << "FAIL " << __FILE__ << ":" \
                << __LINE__ \
                << ": timeout waiting for completion\n"; \
            std::_Exit(1); \
        } \
    } while(0)

// Mock IoAwaitable with a scripted error_code result,
// completing through the environment's executor.
struct ec_op
{
    std::error_code ec_{};

    capy::io_env const* env_ = nullptr;
    capy::continuation cont_{};

    bool await_ready() const noexcept { return false; }

    auto await_suspend(
        std::coroutine_handle<> h,
        capy::io_env const* env)
    {
        env_ = env;
        cont_ = capy::continuation{h};
        env_->executor.post(cont_);
        return std::noop_coroutine();
    }

    std::error_code await_resume() noexcept { return ec_; }
};

// Mock IoAwaitable with a scripted compound (ec, n) result.
struct compound_op
{
    std::error_code ec_{};
    std::size_t n_ = 0;

    capy::io_env const* env_ = nullptr;
    capy::continuation cont_{};

    bool await_ready() const noexcept { return false; }

    auto await_suspend(
        std::coroutine_handle<> h,
        capy::io_env const* env)
    {
        env_ = env;
        cont_ = capy::continuation{h};
        env_->executor.post(cont_);
        return std::noop_coroutine();
    }

    capy::io_result<std::size_t> await_resume() noexcept
    {
        return {ec_, n_};
    }
};

// Mock IoAwaitable that completes with no payload at all
// (the `void` row of make_sigs, distinct from io_result<>'s
// 1-tuple-ec row exercised by async_waker).
struct void_op
{
    capy::io_env const* env_ = nullptr;
    capy::continuation cont_{};

    bool await_ready() const noexcept { return false; }

    auto await_suspend(
        std::coroutine_handle<> h,
        capy::io_env const* env)
    {
        env_ = env;
        cont_ = capy::continuation{h};
        env_->executor.post(cont_);
        return std::noop_coroutine();
    }

    void await_resume() noexcept {}
};

// Mock IoAwaitable returning a plain value type, unrelated to
// error_code (the "other T" row of make_sigs).
struct value_op
{
    capy::io_env const* env_ = nullptr;
    capy::continuation cont_{};

    bool await_ready() const noexcept { return false; }

    auto await_suspend(
        std::coroutine_handle<> h,
        capy::io_env const* env)
    {
        env_ = env;
        cont_ = capy::continuation{h};
        env_->executor.post(cont_);
        return std::noop_coroutine();
    }

    int await_resume() noexcept { return 17; }
};

// Mock IoAwaitable whose await_resume() can throw, exercising
// awaitable_op_state's conditional try/catch -> set_error(exception_ptr).
struct throwing_op
{
    capy::io_env const* env_ = nullptr;
    capy::continuation cont_{};

    bool await_ready() const noexcept { return false; }

    auto await_suspend(
        std::coroutine_handle<> h,
        capy::io_env const* env)
    {
        env_ = env;
        cont_ = capy::continuation{h};
        env_->executor.post(cont_);
        return std::noop_coroutine();
    }

    std::size_t await_resume()
    {
        throw std::runtime_error("throwing_op");
    }
};

void test_compound_success()
{
    auto out = run(capy::as_sender(
        compound_op{.ec_ = {}, .n_ = 42}));
    CHECK(out.ch == channel::value);
    CHECK(out.n == 42);
}

void test_compound_error()
{
    auto expected = std::make_error_code(
        std::errc::connection_reset);
    auto out = run(capy::as_sender(
        compound_op{.ec_ = expected, .n_ = 3}));
    CHECK(out.ch == channel::error);
    CHECK(out.ec == expected);
}

void test_canceled_ec_routes_stopped()
{
    // A canceled disposition surfaces on the stopped channel even
    // though the environment's token was never triggered.
    auto out = run(capy::as_sender(compound_op{
        .ec_ = std::make_error_code(
            std::errc::operation_canceled),
        .n_ = 0}));
    CHECK(out.ch == channel::stopped);
}

void test_value_survives_racing_stop()
{
    // The op ignores tokens and completes with a value; a stop
    // request that merely landed by completion time must not
    // discard the result.
    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    ex::inplace_stop_source ss;
    ss.request_stop();
    std::latch done(1);
    test_outcome out;
    auto op = ex::connect(
        capy::as_sender(compound_op{.ec_ = {}, .n_ = 7}),
        test_receiver<ex::inplace_stop_token>{
            {pool_ex, ss.get_token()}, &out, &done});
    ex::start(op);
    done.wait();
    CHECK(out.ch == channel::value);
    CHECK(out.n == 7);
}

void test_empty_io_result_success()
{
    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    capy::async_waker waker;
    std::latch done(1);
    test_outcome out;
    auto op = ex::connect(
        capy::as_sender(waker.wait()),
        test_receiver<std::stop_token>{
            {pool_ex, {}}, &out, &done});
    ex::start(op);
    waker.wake();
    done.wait();
    CHECK(out.ch == channel::value);
}

void test_void_op_success()
{
    auto out = run(capy::as_sender(void_op{}));
    CHECK(out.ch == channel::value);
}

void test_value_op_success()
{
    auto out = run(capy::as_sender(value_op{}));
    CHECK(out.ch == channel::value);
    CHECK(out.i == 17);
}

void test_throwing_op_error()
{
    auto out = run(capy::as_sender(throwing_op{}));
    CHECK(out.ch == channel::error);
}

void test_ec_success()
{
    auto out = run(capy::as_sender(ec_op{}));
    CHECK(out.ch == channel::value);
}

void test_ec_error()
{
    auto expected = std::make_error_code(
        std::errc::connection_reset);
    auto out = run(capy::as_sender(ec_op{expected}));
    CHECK(out.ch == channel::error);
    CHECK(out.ec == expected);
}

void test_cancel_std_token()
{
    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    capy::async_waker waker;
    std::stop_source ss;
    std::latch done(1);
    test_outcome out;
    auto op = ex::connect(
        capy::as_sender(waker.wait()),
        test_receiver<std::stop_token>{
            {pool_ex, ss.get_token()}, &out, &done});
    ex::start(op);
    ss.request_stop();
    WAIT_OR_DIE(done);
    CHECK(out.ch == channel::stopped);
}

void test_cancel_inplace_token()
{
    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    capy::async_waker waker;
    ex::inplace_stop_source ss;
    std::latch done(1);
    test_outcome out;

    // Rescue thread: if the bridge is broken the waker never
    // observes the stop; wake it late so the test fails on the
    // channel check instead of hanging.
    std::thread rescue([&] {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(200));
        waker.wake();
    });

    auto op = ex::connect(
        capy::as_sender(waker.wait()),
        test_receiver<ex::inplace_stop_token>{
            {pool_ex, ss.get_token()}, &out, &done});
    ex::start(op);
    ss.request_stop();
    done.wait();
    rescue.join();
    CHECK(out.ch == channel::stopped);
}

void test_then_pipeline()
{
    auto out = run(ex::then(
        capy::as_sender(compound_op{.ec_ = {}, .n_ = 41}),
        [](std::size_t n) { return n + 1; }));
    CHECK(out.ch == channel::value);
    CHECK(out.n == 42);
}

void test_read_op_native_success()
{
    auto out = run(capy::read_op::result({}, 7));
    CHECK(out.ch == channel::value);
    CHECK(out.n == 7);
}

void test_read_op_native_error()
{
    auto expected = std::make_error_code(
        std::errc::broken_pipe);
    auto out = run(capy::read_op::result(expected, 0));
    CHECK(out.ch == channel::error);
    CHECK(out.ec == expected);
}

void test_read_op_immediate()
{
    auto out = run(capy::read_op::immediate({}, 9));
    CHECK(out.ch == channel::value);
    CHECK(out.n == 9);
}

void test_read_op_stopped()
{
    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    ex::inplace_stop_source ss;
    std::latch done(1);
    test_outcome out;
    auto o = ex::connect(
        capy::read_op::waits_for_stop(),
        test_receiver<ex::inplace_stop_token>{
            {pool_ex, ss.get_token()}, &out, &done});
    ex::start(o);
    ss.request_stop();
    WAIT_OR_DIE(done);
    CHECK(out.ch == channel::stopped);
}

void test_read_op_stopped_before_start()
{
    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    ex::inplace_stop_source ss;
    std::latch done(1);
    test_outcome out;
    auto o = ex::connect(
        capy::read_op::waits_for_stop(),
        test_receiver<ex::inplace_stop_token>{
            {pool_ex, ss.get_token()}, &out, &done});
    // Stop before start: exercises read_op's pre-check, which
    // must resume inline instead of arming a stop_callback that
    // would fire synchronously during construction.
    ss.request_stop();
    ex::start(o);
    WAIT_OR_DIE(done);
    CHECK(out.ch == channel::stopped);
}

void test_read_op_through_adaptor()
{
    auto out = run(capy::as_sender(capy::read_op::result({}, 5)));
    CHECK(out.ch == channel::value);
    CHECK(out.n == 5);
}

// Symmetric with test_cancel_std_token, but for the native
// awaitable_sender_base path (token_passthrough) instead of as_sender.
void test_read_op_cancel_std_token()
{
    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    std::stop_source ss;
    std::latch done(1);
    test_outcome out;
    auto o = ex::connect(
        capy::read_op::waits_for_stop(),
        test_receiver<std::stop_token>{
            {pool_ex, ss.get_token()}, &out, &done});
    ex::start(o);
    ss.request_stop();
    WAIT_OR_DIE(done);
    CHECK(out.ch == channel::stopped);
}

// Symmetric with test_read_op_immediate, but through as_sender().
void test_adaptor_immediate()
{
    auto out = run(capy::as_sender(
        capy::read_op::immediate({}, 3)));
    CHECK(out.ch == channel::value);
    CHECK(out.n == 3);
}

void test_read_op_then_pipeline()
{
    auto out = run(ex::then(
        capy::read_op::result({}, 41),
        [](std::size_t n) { return n + 1; }));
    CHECK(out.ch == channel::value);
    CHECK(out.n == 42);
}

// sync_wait's environment provides get_scheduler but neither
// get_io_executor nor a stop token; these exercise the scheduler
// bridge end-to-end on the run_loop.
void test_sync_wait_value()
{
    auto r = ex::sync_wait(capy::read_op::result({}, 7));
    CHECK(r.has_value());
    CHECK(std::get<0>(*r) == 7);
}

void test_sync_wait_pipeline()
{
    auto r = ex::sync_wait(ex::then(
        capy::read_op::result({}, 41),
        [](std::size_t n) { return n + 1; }));
    CHECK(r.has_value());
    CHECK(std::get<0>(*r) == 42);
}

void test_sync_wait_error_throws()
{
    bool caught = false;
    try
    {
        (void)ex::sync_wait(capy::read_op::result(
            std::make_error_code(std::errc::broken_pipe), 0));
    }
    catch(...)
    {
        caught = true;
    }
    CHECK(caught);
}

// Records the frame allocator the machinery hands to the op.
struct alloc_probe_op
{
    std::pmr::memory_resource** out_;

    capy::io_env const* env_ = nullptr;
    capy::continuation cont_{};

    bool await_ready() const noexcept { return false; }

    auto await_suspend(
        std::coroutine_handle<> h,
        capy::io_env const* env)
    {
        *out_ = env->frame_allocator;
        env_ = env;
        cont_ = capy::continuation{h};
        env_->executor.post(cont_);
        return std::noop_coroutine();
    }

    std::error_code await_resume() noexcept { return {}; }
};

struct pmr_env
{
    capy::executor_ref ex_;
    std::pmr::memory_resource* mr_;

    auto query(capy::get_io_executor_t const&) const noexcept
        -> capy::executor_ref
    {
        return ex_;
    }

    auto query(ex::get_allocator_t const&) const noexcept
        -> std::pmr::polymorphic_allocator<>
    {
        return std::pmr::polymorphic_allocator<>(mr_);
    }
};

struct pmr_receiver
{
    using receiver_concept = ex::receiver_t;

    pmr_env env_;
    std::latch* done_;

    auto get_env() const noexcept -> pmr_env { return env_; }

    void set_value() && noexcept { done_->count_down(); }
    void set_error(std::error_code) && noexcept
    {
        done_->count_down();
    }
    void set_error(std::exception_ptr) && noexcept
    {
        done_->count_down();
    }
    void set_stopped() && noexcept { done_->count_down(); }
};

void test_env_allocator_reaches_io_env()
{
    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    std::pmr::monotonic_buffer_resource mr;
    std::pmr::memory_resource* seen = nullptr;
    std::latch done(1);
    auto op = ex::connect(
        capy::as_sender(alloc_probe_op{&seen}),
        pmr_receiver{{pool_ex, &mr}, &done});
    ex::start(op);
    done.wait();
    CHECK(seen == &mr);
}

// Cross-protocol consistency: drive the SAME op through co_await
// and through connect/start, and require identical outcomes. The
// dual-protocol contract (see awaitable_sender_base) makes
// divergence impossible when the op reports its disposition
// in-band; these tests pin that.
capy::task<void> record_outcome(
    capy::read_op op,
    test_outcome* out,
    std::latch* done)
{
    auto [ec, n] = co_await std::move(op);
    out->ec = ec;
    out->n = n;
    out->ch = !ec ? channel::value
        : ec == std::errc::operation_canceled
            ? channel::stopped
            : channel::error;
    done->count_down();
}

void test_coawait_matches_sender_success()
{
    auto sender_out = run(capy::read_op::result({}, 7));

    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    test_outcome coawait_out;
    std::latch done(1);
    capy::run_async(pool_ex)(record_outcome(
        capy::read_op::result({}, 7), &coawait_out, &done));
    WAIT_OR_DIE(done);

    CHECK(coawait_out.ch == channel::value);
    CHECK(sender_out.ch == coawait_out.ch);
    CHECK(sender_out.n == coawait_out.n);
}

void test_coawait_matches_sender_error()
{
    auto expected = std::make_error_code(
        std::errc::broken_pipe);
    auto sender_out = run(capy::read_op::result(expected, 0));

    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    test_outcome coawait_out;
    std::latch done(1);
    capy::run_async(pool_ex)(record_outcome(
        capy::read_op::result(expected, 0),
        &coawait_out, &done));
    WAIT_OR_DIE(done);

    CHECK(coawait_out.ch == channel::error);
    CHECK(sender_out.ch == coawait_out.ch);
    CHECK(sender_out.ec == coawait_out.ec);
}

void test_coawait_stopped_before_start()
{
    // co_await twin of test_read_op_stopped_before_start: the
    // pre-stopped token must yield a canceled disposition.
    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    std::stop_source ss;
    ss.request_stop();
    test_outcome coawait_out;
    std::latch done(1);
    capy::run_async(pool_ex, ss.get_token())(record_outcome(
        capy::read_op::waits_for_stop(),
        &coawait_out, &done));
    WAIT_OR_DIE(done);
    CHECK(coawait_out.ch == channel::stopped);
}

void test_coawait_stopped_midflight()
{
    // co_await twin of test_read_op_stopped.
    capy::thread_pool pool(1);
    auto pool_ex = pool.get_executor();
    std::stop_source ss;
    test_outcome coawait_out;
    std::latch done(1);
    capy::run_async(pool_ex, ss.get_token())(record_outcome(
        capy::read_op::waits_for_stop(),
        &coawait_out, &done));
    ss.request_stop();
    WAIT_OR_DIE(done);
    CHECK(coawait_out.ch == channel::stopped);
}

// Channel splitting is keyed on io_result, not tuple shape: a
// std::tuple that happens to lead with error_code is a value.
struct tuple_result_op
{
    bool await_ready() const noexcept { return false; }
    auto await_suspend(
        std::coroutine_handle<>, capy::io_env const*)
    {
        return std::noop_coroutine();
    }
    std::tuple<std::error_code, int> await_resume() noexcept
    {
        return {};
    }
};
static_assert(std::is_same_v<
    decltype(capy::detail::make_sigs<tuple_result_op>()),
    ex::completion_signatures<
        ex::set_value_t(std::tuple<std::error_code, int>),
        ex::set_stopped_t()>>);

// AwaitableSender partitions the world correctly: read_op models
// both protocols; compound_op is awaitable-only; the as_sender
// wrapper is sender-only (no await_suspend).
static_assert(capy::AwaitableSender<capy::read_op>);
static_assert(capy::IoAwaitable<compound_op> &&
    !capy::AwaitableSender<compound_op>);
static_assert(
    !capy::IoAwaitable<capy::awaitable_sender<compound_op>> &&
    !capy::AwaitableSender<capy::awaitable_sender<compound_op>>);

// ensure_sender: identity for dual-protocol ops, lifting for
// awaitable-only ops.
static_assert(std::is_same_v<
    decltype(capy::ensure_sender(
        capy::read_op::result({}, 0))),
    capy::read_op>);
static_assert(std::is_same_v<
    decltype(capy::ensure_sender(compound_op{})),
    capy::awaitable_sender<compound_op>>);

void test_ensure_sender_passthrough()
{
    auto out = run(capy::ensure_sender(
        capy::read_op::result({}, 7)));
    CHECK(out.ch == channel::value);
    CHECK(out.n == 7);
}

void test_ensure_sender_lifts()
{
    auto out = run(capy::ensure_sender(
        compound_op{.ec_ = {}, .n_ = 5}));
    CHECK(out.ch == channel::value);
    CHECK(out.n == 5);
}

// beman never calls the C++26 static-template signature query, so
// nothing else instantiates its body; pin it here so it stays
// callable in constant evaluation and agrees with the beman-compat
// instance form.
static_assert(std::is_same_v<
    decltype(capy::read_op::
        get_completion_signatures<capy::read_op>()),
    decltype(std::declval<capy::read_op const&>()
        .get_completion_signatures(0))>);

// Compile-fail probe: uncomment to verify the non-aggregate
// static_assert in awaitable_sender_base fires. (Completion
// signatures are deduced from await_resume(), so a signature
// mismatch is no longer expressible; the aggregate requirement
// is the remaining connect-time check.)
// Expected diagnostic: "Derived must declare a constructor".
//
// struct aggregate_op
//     : capy::awaitable_sender_base<aggregate_op>
// {
//     bool await_ready() const noexcept { return false; }
//     auto await_suspend(
//         std::coroutine_handle<>, capy::io_env const*)
//     {
//         return std::noop_coroutine();
//     }
//     capy::io_result<std::size_t> await_resume() noexcept
//     {
//         return {};
//     }
// };
// void probe() { (void)run(aggregate_op{}); }

int main()
{
    test_empty_io_result_success();
    test_void_op_success();
    test_value_op_success();
    test_throwing_op_error();
    test_ec_success();
    test_ec_error();
    test_compound_success();
    test_compound_error();
    test_canceled_ec_routes_stopped();
    test_value_survives_racing_stop();
    test_cancel_std_token();
    test_cancel_inplace_token();
    test_then_pipeline();
    test_read_op_native_success();
    test_read_op_native_error();
    test_read_op_immediate();
    test_read_op_stopped();
    test_read_op_stopped_before_start();
    test_read_op_through_adaptor();
    test_read_op_then_pipeline();
    test_sync_wait_value();
    test_sync_wait_pipeline();
    test_sync_wait_error_throws();
    test_env_allocator_reaches_io_env();
    test_coawait_matches_sender_success();
    test_coawait_matches_sender_error();
    test_coawait_stopped_before_start();
    test_coawait_stopped_midflight();
    test_ensure_sender_passthrough();
    test_ensure_sender_lifts();
    test_read_op_cancel_std_token();
    test_adaptor_immediate();

    if(failures == 0)
        std::cout << "all tests passed\n";
    else
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
