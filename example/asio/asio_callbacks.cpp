//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "api/uni_stream.hpp"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <cstdio>
#include <cstring>
#include <memory>

constexpr std::size_t total_bytes = 1024;

struct do_write
{
    struct state
    {
        uni_stream& stream;
        std::size_t total;
        std::size_t written = 0;
        char buf[128];

        state(uni_stream& s, std::size_t t)
            : stream(s)
            , total(t)
        {
            std::memset(buf, 'X', sizeof(buf));
        }
    };

    std::shared_ptr<state> state_;

    do_write(uni_stream& stream, std::size_t total)
        : state_(std::make_shared<state>(stream, total))
    {
    }

    void operator()()
    {
        if(state_->written >= state_->total)
        {
            std::printf("writer: done, wrote %zu bytes\n", state_->written);
            return;
        }

        std::size_t chunk = (std::min)(sizeof(state_->buf), state_->total - state_->written);

        net::async_write(
            state_->stream,
            net::buffer(state_->buf, chunk),
            [self = *this](boost::system::error_code ec, std::size_t n) mutable
            {
                if(ec)
                {
                    std::printf("writer error: %s\n", ec.message().c_str());
                    return;
                }
                self.state_->written += n;
                std::printf("writer: wrote %zu bytes (total %zu)\n", n, self.state_->written);
                self();
            });
    }
};

struct do_read
{
    struct state
    {
        uni_stream& stream;
        std::size_t total;
        std::size_t read_total = 0;
        char buf[128];

        state(uni_stream& s, std::size_t t)
            : stream(s)
            , total(t)
        {
        }
    };

    std::shared_ptr<state> state_;

    do_read(uni_stream& stream, std::size_t total)
        : state_(std::make_shared<state>(stream, total))
    {
    }

    void operator()()
    {
        if(state_->read_total >= state_->total)
        {
            std::printf("reader: done, read %zu bytes\n", state_->read_total);
            std::printf("example complete!\n");
            return;
        }

        std::size_t remaining = state_->total - state_->read_total;
        std::size_t to_read = (std::min)(sizeof(state_->buf), remaining);

        net::async_read(
            state_->stream,
            net::buffer(state_->buf, to_read),
            [self = *this](boost::system::error_code ec, std::size_t n) mutable
            {
                if(ec)
                {
                    std::printf("reader error: %s\n", ec.message().c_str());
                    return;
                }
                self.state_->read_total += n;
                std::printf("reader: read %zu bytes (total %zu)\n", n, self.state_->read_total);
                self();
            });
    }
};

int main()
{
    // Type-erased asio::io_context
    asio_context ctx;

    // Return two connected uni_stream
    auto [client, server] = make_uni_pair(ctx);

    // Start the writer (callback chain)
    do_write(client, total_bytes)();

    // Start the reader (callback chain)
    do_read(server, total_bytes)();

    // Run the I/O context, this returns when all work is done
    ctx.run();

    return 0;
}
