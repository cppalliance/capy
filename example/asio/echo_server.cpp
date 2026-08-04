//
// Copyright (c) 2026 Klemens Morgenstern
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Echo Server Example (Boost.Asio)
//
// A complete echo server using Boost.Asio TCP sockets with capy coroutines.
// Demonstrates how to use capy::as_io_awaitable to await ASIO async operations.
//

#include <boost/capy/asio/boost.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/ex/run_async.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace net = boost::asio;
namespace capy = boost::capy;

// Socket type with as_io_awaitable as the default completion token.
// This allows omitting the completion token in async calls.
using tcp_socket = capy::as_io_awaitable_t::as_default_on_t<
    net::ip::tcp::socket>;

using tcp_acceptor = capy::as_io_awaitable_t::as_default_on_t<
    net::ip::tcp::acceptor>;

// Handle a single client session.
// Reads data from the socket and echoes it back until the connection closes.
capy::io_task<> echo_session(tcp_socket sock)
{
    char buf[1024];
        
    for (;;)
    {
        // Read some data from the client.
        auto res = co_await sock.async_read_some(net::buffer(buf));

        if (boost::system::error_code ec = res.ec; ec)
        {
            if (ec != net::error::eof)
                std::cerr << "Read error: " << ec.message() << "\n";
            break;
        }

        // Write the data back to the client.
        res = co_await net::async_write(
            sock, net::buffer(buf, std::get<0>(res.values)));

        if (res.ec)
        {
            std::cerr << "Write error: " << res.ec.message() << "\n";
            break;
        }
    }

    boost::system::error_code ec;
    sock.shutdown(net::ip::tcp::socket::shutdown_both, ec);
    sock.close(ec);

    co_return {};
}

// Accept loop - accepts connections and spawns echo sessions.
capy::io_task<> accept_loop(tcp_acceptor& acceptor)
{
    auto exec = capy::wrap_asio_executor(acceptor.get_executor());
    auto ep = acceptor.local_endpoint();
    std::cout << "Listening on port " << ep.port() << "\n";

    for (;;)
    {
        // Accept a new connection.
        auto [ec, sock] = co_await acceptor.async_accept();

        if (ec)
        {
            std::cerr << "Accept error: " << ec.message() << "\n";
            continue;
        }

        auto remote = sock.remote_endpoint();
        std::cout << "Connection from " << remote.address()
                  << ":" << remote.port() << "\n";

        // Spawn an echo session for this connection.
        // Convert the socket to use as_io_awaitable as default.
        capy::run_async(exec)(
            echo_session(capy::as_io_awaitable_t::as_default_on(std::move(sock))));
    }
}

int main(int argc, char* argv[])
{
    unsigned short port = 8080;
    if (argc > 1)
        port = static_cast<unsigned short>(std::atoi(argv[1]));

    try
    {
        net::io_context ioc;

        // Create the acceptor with as_io_awaitable as default.
        tcp_acceptor acceptor(
            ioc,
            net::ip::tcp::endpoint(net::ip::tcp::v4(), port));

        // Wrap the ASIO executor for use with capy.
        auto exec = capy::wrap_asio_executor(ioc.get_executor());

        // Spawn the accept loop.
        capy::run_async(exec)(accept_loop(acceptor));

        // Run the I/O context.
        ioc.run();
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
