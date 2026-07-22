//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Echo Server Example (Corosio)
//
// A complete echo server using Corosio for real network I/O.
// Demonstrates Capy coroutines driving actual TCP connections.
//

// tag::full[]
#include <boost/capy.hpp>
#include <boost/corosio.hpp>
#include <iostream>

namespace corosio = boost::corosio;
namespace capy = boost::capy;

capy::task<> echo_session(corosio::tcp_socket sock)
{
    char buf[1024];

    for (;;)
    {
        // tag::session_io[]
        auto [ec, n] = co_await sock.read_some(
            capy::mutable_buffer(buf, sizeof(buf)));

        auto [wec, wn] = co_await capy::write(
            sock, capy::const_buffer(buf, n));
        // end::session_io[]

        if (ec)
            break;

        if (wec)
            break;
    }

    sock.close();
}

capy::task<> accept_loop(
    corosio::tcp_acceptor& acc,
    corosio::io_context& ioc)
{
    auto ep = acc.local_endpoint();
    std::cout << "Listening on port " << ep.port() << "\n";

    // tag::accept[]
    for (;;)
    {
        corosio::tcp_socket peer(ioc);
        auto [ec] = co_await acc.accept(peer);
        // end::accept[]

        if (ec)
        {
            std::cout << "Accept error: " << ec.message() << "\n";
            continue;
        }

        auto remote = peer.remote_endpoint();
        std::cout << "Connection from ";
        if (remote.is_v4())
            std::cout << remote.v4_address();
        else
            std::cout << remote.v6_address();
        std::cout << ":" << remote.port() << "\n";

        // tag::spawn_session[]
        capy::run_async(ioc.get_executor())(
            echo_session(std::move(peer)));
        // end::spawn_session[]
        // tag::accept[]
    }
    // end::accept[]
}

int main(int argc, char* argv[])
{
    unsigned short port = 8080;
    if (argc > 1)
        port = static_cast<unsigned short>(std::atoi(argv[1]));

    // tag::acceptor[]
    corosio::io_context ioc;
    corosio::tcp_acceptor acc(ioc, corosio::endpoint(port));
    // end::acceptor[]

    capy::run_async(ioc.get_executor())(
        accept_loop(acc, ioc));

    ioc.run();

    return 0;
}
// end::full[]
