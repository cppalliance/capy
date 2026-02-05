//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "asio.hpp"

#include <boost/capy/buffers.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/ex/run_async.hpp>

#include <cstdio>
#include <cstring>
#include <span>

constexpr std::size_t total_bytes = 1024;

capy::task<>
writer(
    capy::any_stream& stream,
    std::size_t total)
{
    char buf[128];
    std::memset(buf, 'X', sizeof(buf));

    std::size_t written = 0;
    while(written < total)
    {
        std::size_t chunk = (std::min)(sizeof(buf), total - written);
        capy::const_buffer cb(buf, chunk);
        auto [ec, n] = co_await stream.write_some(std::span(&cb, 1));
        if(ec)
        {
            std::printf("writer error: %s\n", ec.message().c_str());
            co_return;
        }
        written += n;
        std::printf("writer: wrote %zu bytes (total %zu)\n", n, written);
    }
    std::printf("writer: done, wrote %zu bytes\n", written);
}

capy::task<>
reader(
    capy::any_stream& stream,
    std::size_t total)
{
    char buf[128];

    std::size_t read_total = 0;
    while(read_total < total)
    {
        capy::mutable_buffer mb(buf, sizeof(buf));
        auto [ec, n] = co_await stream.read_some(std::span(&mb, 1));
        if(ec)
        {
            std::printf("reader error: %s\n", ec.message().c_str());
            co_return;
        }
        read_total += n;
        std::printf("reader: read %zu bytes (total %zu)\n", n, read_total);
    }
    std::printf("reader: done, read %zu bytes\n", read_total);
}

capy::task<>
run_example(
    capy::any_stream& client_stream,
    capy::any_stream& server_stream)
{
    co_await capy::when_all(
        writer(client_stream, total_bytes),
        reader(server_stream, total_bytes));

    std::printf("example complete!\n");
}

int main()
{
    // Type-erased asio::io_context
    asio_context ctx;

    // Return two connected type-erased asio::ip::tcp::socket
    auto [s1, s2] = make_stream_pair(ctx);

    // Launch the run_example coroutine with the streams
    capy::run_async(ctx.get_executor())( run_example(s1, s2) );

    // Run the I/O context, this returns when all work is done
    ctx.run();

    return 0;
}
