//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "api/capy_streams.hpp"
#include "api/use_capy.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <coroutine>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/ex/run_async.hpp>

#include <cstdio>
#include <cstring>

constexpr std::size_t total_bytes = 1024;

// Writer coroutine using use_capy with raw Asio socket.
// Returns io_task<> so it can be composed with when_all.
// tag::writer[]
capy::io_task<>
writer(
    net::ip::tcp::socket& socket,
    std::size_t total)
{
    char buf[128];
    std::memset(buf, 'X', sizeof(buf));

    std::size_t written = 0;
    while (written < total)
    {
        std::size_t chunk = (std::min)(sizeof(buf), total - written);

        // Use Asio's async_write_some with use_capy completion token
        auto [ec, n] = co_await socket.async_write_some(
            net::buffer(buf, chunk), use_capy);

        if (ec)
        {
            std::printf("writer error: %s\n", ec.message().c_str());
            co_return capy::io_result<>{ec};
        }
        written += n;
        std::printf("writer: wrote %zu bytes (total %zu)\n", n, written);
    }
    std::printf("writer: done, wrote %zu bytes\n", written);
    co_return capy::io_result<>{};
}
// end::writer[]

// Reader coroutine using use_capy with raw Asio socket.
// Returns io_task<> so it can be composed with when_all.
capy::io_task<>
reader(
    net::ip::tcp::socket& socket,
    std::size_t total)
{
    char buf[128];

    std::size_t read_total = 0;
    while (read_total < total)
    {
        // Use Asio's async_read_some with use_capy completion token
        auto [ec, n] = co_await socket.async_read_some(
            net::buffer(buf), use_capy);

        if (ec)
        {
            std::printf("reader error: %s\n", ec.message().c_str());
            co_return capy::io_result<>{ec};
        }
        read_total += n;
        std::printf("reader: read %zu bytes (total %zu)\n", n, read_total);
    }
    std::printf("reader: done, read %zu bytes\n", read_total);
    co_return capy::io_result<>{};
}

capy::task<>
run_example(
    net::ip::tcp::socket& client,
    net::ip::tcp::socket& server)
{
    auto r = co_await capy::when_all(
        writer(client, total_bytes),
        reader(server, total_bytes));

    if (r.ec)
        std::printf("example error: %s\n", r.ec.message().c_str());
    else
        std::printf("example complete!\n");
}

int main()
{
    // Type-erased asio::io_context
    asio_context ctx;
    auto& ioc = ctx.context();

    // Create connected socket pair
    net::ip::tcp::acceptor acceptor(
        ioc, net::ip::tcp::endpoint(net::ip::tcp::v4(), 0));
    auto ep = acceptor.local_endpoint();

    net::ip::tcp::socket client(ioc);
    net::ip::tcp::socket server(ioc);

    auto connect_ep = net::ip::tcp::endpoint(
        net::ip::address_v4::loopback(), ep.port());

    client.async_connect(connect_ep, [](boost::system::error_code) {});
    acceptor.async_accept(server, [](boost::system::error_code) {});

    ioc.run();
    ioc.restart();

    // Launch the example coroutine
    capy::run_async(ctx.get_executor())(run_example(client, server));

    // Run the I/O context
    ctx.run();

    return 0;
}
