//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef CAPY_EXAMPLE_USE_CAPY_HPP
#define CAPY_EXAMPLE_USE_CAPY_HPP

#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/deferred.hpp>

#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace net = boost::asio;
namespace capy = boost::capy;

namespace detail {

//----------------------------------------------------------
//
// cancel_bridge - Bridges std::stop_token to Asio cancellation
//
//----------------------------------------------------------

struct cancel_bridge
{
    net::cancellation_signal signal;

    struct handler
    {
        net::cancellation_signal* sig;
        void operator()() const noexcept
        {
            sig->emit(net::cancellation_type::terminal);
        }
    };

    std::optional<std::stop_callback<handler>> stop_cb;

    explicit cancel_bridge(std::stop_token const& st)
    {
        if (st.stop_possible())
            stop_cb.emplace(st, handler{&signal});
    }
};

//----------------------------------------------------------
//
// result_type_for - Maps completion signature to io_result
//
//----------------------------------------------------------

template<typename... Args>
struct result_type_for;

template<>
struct result_type_for<boost::system::error_code>
{
    using type = capy::io_result<>;
};

template<typename T1>
struct result_type_for<boost::system::error_code, T1>
{
    using type = capy::io_result<std::decay_t<T1>>;
};

template<typename T1, typename T2>
struct result_type_for<boost::system::error_code, T1, T2>
{
    using type = capy::io_result<std::decay_t<T1>, std::decay_t<T2>>;
};

template<typename T1, typename T2, typename T3>
struct result_type_for<boost::system::error_code, T1, T2, T3>
{
    using type = capy::io_result<std::decay_t<T1>, std::decay_t<T2>, std::decay_t<T3>>;
};

template<typename... Args>
using result_type_for_t = typename result_type_for<Args...>::type;

//----------------------------------------------------------
//
// capy_awaitable - IoAwaitable wrapping a deferred operation
//
//----------------------------------------------------------

template<typename DeferredOp, typename... Args>
class capy_awaitable
{
    using result_type = result_type_for_t<Args...>;

    DeferredOp op_;
    result_type result_;
    std::shared_ptr<cancel_bridge> cancel_;

public:
    explicit capy_awaitable(DeferredOp op)
        : op_(std::move(op))
    {
    }

    bool await_ready() const noexcept
    {
        return false;
    }

    capy::coro await_suspend(
        capy::coro h,
        capy::executor_ref ex,
        std::stop_token st)
    {
        cancel_ = std::make_shared<cancel_bridge>(st);

        auto handler = [this, h, ex](Args... args) mutable
        {
            store_result(std::move(args)...);
            ex.dispatch(h);
        };

        std::move(op_)(
            net::bind_cancellation_slot(
                cancel_->signal.slot(),
                std::move(handler)));

        return std::noop_coroutine();
    }

    result_type await_resume()
    {
        cancel_.reset();
        return std::move(result_);
    }

private:
    void store_result(boost::system::error_code ec)
    {
        result_.ec = ec;
    }

    template<typename T1>
    void store_result(boost::system::error_code ec, T1&& t1)
    {
        result_.ec = ec;
        result_.t1 = std::forward<T1>(t1);
    }

    template<typename T1, typename T2>
    void store_result(boost::system::error_code ec, T1&& t1, T2&& t2)
    {
        result_.ec = ec;
        result_.t1 = std::forward<T1>(t1);
        result_.t2 = std::forward<T2>(t2);
    }

    template<typename T1, typename T2, typename T3>
    void store_result(boost::system::error_code ec, T1&& t1, T2&& t2, T3&& t3)
    {
        result_.ec = ec;
        result_.t1 = std::forward<T1>(t1);
        result_.t2 = std::forward<T2>(t2);
        result_.t3 = std::forward<T3>(t3);
    }
};

} // namespace detail

//----------------------------------------------------------
//
// use_capy_t - Completion token for Asio operations
//
//----------------------------------------------------------

/** Completion token that returns an IoAwaitable.

    This completion token can be passed to any Asio async operation
    to receive an awaitable that satisfies capy's IoAwaitable concept.
    The awaitable uses the extended await_suspend signature that
    accepts an executor and stop token.

    Internally uses Asio's deferred_async_operation to capture the
    operation without std::function overhead.

    @par Example
    @code
    capy::task<> example(net::ip::tcp::socket& sock)
    {
        char buf[1024];
        auto [ec, n] = co_await sock.async_read_some(
            net::buffer(buf), use_capy);
        if (ec) { ... }
    }
    @endcode
*/
struct use_capy_t
{
    constexpr use_capy_t() = default;
};

/// Completion token instance for use with Asio async operations.
inline constexpr use_capy_t use_capy;

//----------------------------------------------------------
//
// async_result specialization for use_capy_t
//
//----------------------------------------------------------

namespace boost {
namespace asio {

template<typename R, typename... Args>
class async_result<use_capy_t, R(Args...)>
{
public:
    template<typename Initiation, typename... InitArgs>
    static auto initiate(
        Initiation&& initiation,
        use_capy_t,
        InitArgs&&... args)
    {
        // Create a deferred_async_operation that stores initiation + args
        auto deferred_op = deferred_async_operation<
            R(Args...), std::decay_t<Initiation>, std::decay_t<InitArgs>...>(
                deferred_init_tag{},
                std::forward<Initiation>(initiation),
                std::forward<InitArgs>(args)...);

        return ::detail::capy_awaitable<
            decltype(deferred_op), std::decay_t<Args>...>(
                std::move(deferred_op));
    }
};

} // namespace asio
} // namespace boost

#endif
