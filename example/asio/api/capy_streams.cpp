//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "capy_streams.hpp"

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/asio.hpp>
#include <boost/capy/concept/stream.hpp>
#include <coroutine>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <stop_token>

//----------------------------------------------------------
//
// cancel_state - Bridges std::stop_token to Asio cancellation
//
//----------------------------------------------------------

struct cancel_state
{
    net::cancellation_signal signal;

    struct handler {
        net::cancellation_signal* sig;
        void operator()() const noexcept {
            sig->emit(net::cancellation_type::terminal);
        }
    };
    std::optional<std::stop_callback<handler>> stop_cb;

    explicit cancel_state(std::stop_token st)
    {
        stop_cb.emplace(st, handler{&signal});
    }
};

//----------------------------------------------------------
//
// asio_socket - Wraps asio::ip::tcp::socket for capy
//
//----------------------------------------------------------

class asio_socket
{
    net::ip::tcp::socket socket_;

public:
    explicit asio_socket(net::ip::tcp::socket sock)
        : socket_(std::move(sock))
    {
    }

    asio_socket(asio_socket&&) = default;
    asio_socket& operator=(asio_socket&&) = default;

    net::ip::tcp::socket& socket() noexcept { return socket_; }

    // read_some awaitable

    template<capy::MutableBufferSequence MB>
    class read_awaitable
    {
        asio_socket* self_;
        MB buffers_;
        capy::io_result<std::size_t> result_;
        std::shared_ptr<cancel_state> cancel_;
        capy::continuation cont_;

    public:
        read_awaitable(
            asio_socket* self,
            MB buffers)
            : self_(self)
            , buffers_(buffers)
        {
        }

        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h,
            capy::io_env const* env)
        {
            cont_.h = h;
            cancel_ = std::make_shared<cancel_state>(env->stop_token);

            self_->socket_.async_read_some(
                capy::to_asio(buffers_),
                net::bind_cancellation_slot(
                    cancel_->signal.slot(),
                    [this, ex = env->executor](
                        boost::system::error_code ec,
                        std::size_t n) mutable
                    {
                        std::get<0>(result_) = ec;
                        std::get<1>(result_) = n;
                        ex.post(cont_);
                    }));

            return std::noop_coroutine();
        }

        capy::io_result<std::size_t>
        await_resume()
        {
            cancel_.reset();
            return result_;
        }
    };

    template<capy::MutableBufferSequence MB>
    read_awaitable<MB>
    read_some(MB buffers)
    {
        return read_awaitable<MB>(this, buffers);
    }

    // write_some awaitable

    template<capy::ConstBufferSequence CB>
    class write_awaitable
    {
        asio_socket* self_;
        CB buffers_;
        capy::io_result<std::size_t> result_;
        std::shared_ptr<cancel_state> cancel_;
        capy::continuation cont_;

    public:
        write_awaitable(
            asio_socket* self,
            CB buffers)
            : self_(self)
            , buffers_(buffers)
        {
        }

        bool await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(
            std::coroutine_handle<> h,
            capy::io_env const* env)
        {
            cont_.h = h;
            cancel_ = std::make_shared<cancel_state>(env->stop_token);

            self_->socket_.async_write_some(
                capy::to_asio(buffers_),
                net::bind_cancellation_slot(
                    cancel_->signal.slot(),
                    [this, ex = env->executor](
                        boost::system::error_code ec,
                        std::size_t n) mutable
                    {
                        std::get<0>(result_) = ec;
                        std::get<1>(result_) = n;
                        ex.post(cont_);
                    }));

            return std::noop_coroutine();
        }

        capy::io_result<std::size_t>
        await_resume()
        {
            cancel_.reset();
            return result_;
        }
    };

    template<capy::ConstBufferSequence CB>
    write_awaitable<CB>
    write_some(CB buffers)
    {
        return write_awaitable<CB>(this, buffers);
    }
};

static_assert(capy::ReadStream<asio_socket>);
static_assert(capy::WriteStream<asio_socket>);

//----------------------------------------------------------
//
// asio_executor - Wraps asio::io_context::executor for capy
//
//----------------------------------------------------------

class asio_executor
{
    net::io_context::executor_type ex_;
    asio_context* ctx_;

public:
    asio_executor(
        net::io_context::executor_type ex,
        asio_context* ctx) noexcept
        : ex_(ex)
        , ctx_(ctx)
    {
    }

    bool operator==(asio_executor const& other) const noexcept
    {
        return ex_ == other.ex_;
    }

    asio_context& context() const noexcept
    {
        return *ctx_;
    }

    void on_work_started() const noexcept {}
    void on_work_finished() const noexcept {}

    std::coroutine_handle<> dispatch(capy::continuation& c) const
    {
        auto h = c.h;
        net::post(ex_, [h]{ h.resume(); });
        return std::noop_coroutine();
    }

    void post(capy::continuation& c) const
    {
        auto h = c.h;
        net::post(ex_, [h]{ h.resume(); });
    }
};

//----------------------------------------------------------
//
// asio_context::impl
//
//----------------------------------------------------------

struct asio_context::impl
{
    asio_context* self;
    net::io_context ioc;
    asio_executor ex;

    explicit impl(asio_context* s)
        : self(s)
        , ex(ioc.get_executor(), self)
    {
    }
};

//----------------------------------------------------------
//
// asio_context
//
//----------------------------------------------------------

asio_context::asio_context()
    : impl_(new impl(this))
{
}

asio_context::~asio_context()
{
    delete impl_;
}

net::io_context&
asio_context::context() noexcept
{
    return impl_->ioc;
}

capy::executor_ref
asio_context::get_executor() noexcept
{
    return impl_->ex;
}

void
asio_context::run()
{
    impl_->ioc.run();
}

//----------------------------------------------------------
//
// make_socket_pair
//
//----------------------------------------------------------

static
std::pair<asio_socket, asio_socket>
make_socket_pair(asio_context& ctx)
{
    auto& ioc = ctx.context();

    net::ip::tcp::acceptor acceptor(
        ioc, net::ip::tcp::endpoint(net::ip::tcp::v4(), 0));

    auto ep = acceptor.local_endpoint();

    net::ip::tcp::socket client_sock(ioc);
    net::ip::tcp::socket server_sock(ioc);

    auto connect_ep = net::ip::tcp::endpoint(
        net::ip::address_v4::loopback(), ep.port());

    client_sock.async_connect(
        connect_ep,
        [](boost::system::error_code) {});

    acceptor.async_accept(
        server_sock,
        [](boost::system::error_code) {});

    ioc.run();
    ioc.restart();

    return {
        asio_socket(std::move(client_sock)),
        asio_socket(std::move(server_sock))
    };
}

//----------------------------------------------------------
//
// make_stream_pair
//
//----------------------------------------------------------

std::pair<capy::any_stream, capy::any_stream>
make_stream_pair(asio_context& ctx)
{
    auto [client, server] = make_socket_pair(ctx);
    return {
        capy::any_stream(std::move(client)),
        capy::any_stream(std::move(server))
    };
}
