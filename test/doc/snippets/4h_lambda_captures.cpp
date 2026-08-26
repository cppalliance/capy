//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/4.coroutines/4h.lambda-captures.adoc.
// The fragments that demonstrate dangling captures are compiled but
// never executed: running them would be undefined behavior.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/ex/any_executor.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include <cstddef>
#include <string>
#include <system_error>
#include <utility>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {


struct io_step
{
    std::error_code ec;
    std::size_t n = 0;
};

int socket_reads = 0;

// Stand-in socket that completes immediately, so the safe fragments
// can run to completion under the blocking test executor.
struct socket
{
    capy::task<io_step> read_some(capy::mutable_buffer)
    {
        ++socket_reads;
        co_return io_step{};
    }
};

capy::any_executor executor;

int log_count = 0;

void log(char const*, std::string const&)
{
    ++log_count;
}

capy::task<> handle_request()
{
    co_return;
}

namespace dangling_capture {

// tag::dangling_capture[]
namespace capy = boost::capy;

void process(socket& sock)
{
    // The lambda is created and called on the spot. `started` holds the
    // task the call returned -- not the lambda itself.
    capy::task<> started = [&sock]() -> capy::task<>
    {
        char buf[1024];
        auto [ec, n] = co_await sock.read_some(capy::make_buffer(buf));
    }();  // <-- called here, so the lambda is a temporary and dies now

    capy::run_async(executor)(std::move(started));
}
// end::dangling_capture[]

} // namespace dangling_capture

// Compiles but is never called: the closure dies before the coroutine
// resumes, so running it would be undefined behavior.
[[maybe_unused]] void (* const dangling_capture_demo)(socket&) =
    &dangling_capture::process;

namespace iife_param {

// tag::iife_parameter[]
namespace capy = boost::capy;

void process(socket& sock)
{
    auto task = [](socket* s) -> capy::task<>
    {
        char buf[1024];
        auto [ec, n] = co_await s->read_some(capy::make_buffer(buf));
    }(&sock);

    capy::run_async(executor)(std::move(task));
}
// end::iife_parameter[]

} // namespace iife_param

namespace capture_this {

// The class compiles but nothing ever calls run(): invoking the
// returned task would access a destroyed closure.
// tag::capture_this_broken[]
class connection_handler
{
    socket sock_;
    std::string name_;

public:
    capy::task<> run()
    {
        // BROKEN: 'this' captured in lambda, lambda destroyed after invoke
        return [this]() -> capy::task<>
        {
            log("Connection from", name_);  // UB: 'this' is dangling
            co_await handle_request();
        }();
    }
};
// end::capture_this_broken[]

} // namespace capture_this

namespace parameter_self {

// tag::parameter_self_correct[]
class connection_handler
{
    socket sock_;
    std::string name_;

    capy::task<> handle_request();

public:
    capy::task<> run()
    {
        // CORRECT: 'self' is a parameter, copied to coroutine frame
        return [](connection_handler* self) -> capy::task<>
        {
            log("Connection from", self->name_);
            co_await self->handle_request();
        }(this);
    }
};
// end::parameter_self_correct[]

capy::task<> connection_handler::handle_request()
{
    co_return;
}

} // namespace parameter_self

namespace named_member {

// tag::named_member_coroutine[]
class connection_handler
{
    socket sock_;

    capy::task<> do_handle()
    {
        // 'this' is an implicit parameter, handled correctly
        char buf[1024];
        co_await sock_.read_some(capy::make_buffer(buf));
    }

public:
    capy::task<> run()
    {
        return do_handle();
    }
};
// end::named_member_coroutine[]

} // namespace named_member

struct lambda_captures_test
{
    void
    testIifeParameter()
    {
        capy::test::blocking_context ctx;
        executor = ctx.get_executor();
        socket sock;
        int const before = socket_reads;
        iife_param::process(sock);
        BOOST_TEST(socket_reads == before + 1);
        executor = capy::any_executor();
    }

    void
    testParameterSelf()
    {
        parameter_self::connection_handler h;
        int const before = log_count;
        capy::test::run_blocking()(h.run());
        BOOST_TEST(log_count == before + 1);
    }

    void
    testStoredLambda()
    {
        socket sock;
        int const before = socket_reads;
        // tag::stored_lambda_safe[]
        // SAFE: lambda stored in 'handler', outlives coroutine
        auto handler = [&sock]() -> capy::task<>
        {
            char buf[1024];
            co_await sock.read_some(capy::make_buffer(buf));
        };

        // Lambda 'handler' still exists here
        // Blocks until coroutine completes
        capy::test::run_blocking()(handler());
        // Lambda destroyed after coroutine finishes
        // end::stored_lambda_safe[]
        BOOST_TEST(socket_reads == before + 1);
    }

    void
    testNamedMemberCoroutine()
    {
        named_member::connection_handler h;
        int const before = socket_reads;
        capy::test::run_blocking()(h.run());
        BOOST_TEST(socket_reads == before + 1);
    }

    void
    run()
    {
        testIifeParameter();
        testParameterSelf();
        testStoredLambda();
        testNamedMemberCoroutine();
    }
};

} // namespace

TEST_SUITE(lambda_captures_test, "boost.capy.doc.4h_lambda_captures");
