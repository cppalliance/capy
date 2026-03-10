//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_STREAM_HPP
#define BOOST_CAPY_TEST_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <coroutine>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/read.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace boost {
namespace capy {
namespace test {

/** A connected stream for testing bidirectional I/O.

    Streams are created in pairs via @ref make_stream_pair.
    Data written to one end becomes available for reading on
    the other. If no data is available when @ref read_some
    is called, the calling coroutine suspends until the peer
    calls @ref write_some. The shared @ref fuse enables error
    injection at controlled points in both directions.

    When the fuse injects an error or throws on one end, the
    other end is automatically closed: any suspended reader is
    resumed with `error::eof`, and subsequent operations on
    both ends return `error::eof`. Calling @ref close on one
    end signals eof to the peer's reads after draining any
    buffered data, while the peer may still write.

    @par Thread Safety
    Single-threaded only. Both ends of the pair must be
    accessed from the same thread. Concurrent access is
    undefined behavior.

    @par Example
    @code
    fuse f;
    auto [a, b] = make_stream_pair( f );

    auto r = f.armed( [&]( fuse& ) -> task<> {
        auto [ec, n] = co_await a.write_some(
            const_buffer( "hello", 5 ) );
        if( ec )
            co_return;

        char buf[32];
        auto [ec2, n2] = co_await b.read_some(
            mutable_buffer( buf, sizeof( buf ) ) );
        if( ec2 )
            co_return;
        // buf contains "hello"
    } );
    @endcode

    @see make_stream_pair, fuse
*/
class stream
{
    // Single-threaded only. No concurrent access to either
    // end of the pair. Both streams and all operations must
    // run on the same thread.

    struct half
    {
        std::string buf;
        std::size_t max_read_size = std::size_t(-1);
        std::coroutine_handle<> pending_h{};
        executor_ref pending_ex;
        bool eof = false;
    };

    struct state
    {
        fuse f;
        bool closed = false;
        half sides[2];

        explicit state(fuse f_) noexcept
            : f(std::move(f_))
        {
        }

        // Set closed and resume any suspended readers
        // with eof on both sides.
        void close()
        {
            closed = true;
            for(auto& side : sides)
            {
                if(side.pending_h)
                {
                    auto h = side.pending_h;
                    side.pending_h = {};
                    auto ex = side.pending_ex;
                    side.pending_ex = {};
                    ex.post(h);
                }
            }
        }
    };

    // Wraps the maybe_fail() call. If the guard is
    // not disarmed before destruction (fuse returned
    // an error, or threw an exception), closes both
    // ends so any suspended peer gets eof.
    struct close_guard
    {
        state* st;
        bool armed = true;
        void disarm() noexcept { armed = false; }
        ~close_guard() noexcept(false) { if(armed) st->close(); }
    };

    std::shared_ptr<state> state_;
    int index_;

    stream(
        std::shared_ptr<state> sp,
        int index) noexcept
        : state_(std::move(sp))
        , index_(index)
    {
    }

    friend std::pair<stream, stream>
    make_stream_pair(fuse);

public:
    stream(stream const&) = delete;
    stream& operator=(stream const&) = delete;
    stream(stream&&) = default;
    stream& operator=(stream&&) = default;

    /** Signal end-of-stream to the peer.

        Marks the peer's read direction as closed.
        If the peer is suspended in @ref read_some,
        it is resumed. The peer drains any buffered
        data before receiving `error::eof`. Writes
        from the peer are unaffected.
    */
    void
    close()
    {
        int peer = 1 - index_;
        auto& side = state_->sides[peer];
        side.eof = true;
        if(side.pending_h)
        {
            auto h = side.pending_h;
            side.pending_h = {};
            auto ex = side.pending_ex;
            side.pending_ex = {};
            ex.post(h);
        }
    }

    /** Set the maximum bytes returned per read.

        Limits how many bytes @ref read_some returns in
        a single call, simulating chunked network delivery.
        The default is unlimited.

        @param n Maximum bytes per read.
    */
    void
    set_max_read_size(std::size_t n) noexcept
    {
        state_->sides[index_].max_read_size = n;
    }

    /** Asynchronously read data from the stream.

        Transfers up to `buffer_size(buffers)` bytes from
        data written by the peer. If no data is available,
        the calling coroutine suspends until the peer calls
        @ref write_some. Before every read, the attached
        @ref fuse is consulted to possibly inject an error.
        If the fuse fires, the peer is automatically closed.
        If the stream is closed, returns `error::eof`.
        The returned `std::size_t` is the number of bytes
        transferred.

        @param buffers The mutable buffer sequence to receive data.

        @return An awaitable that await-returns `(error_code,std::size_t)`.

        @see fuse, close
    */
    template<MutableBufferSequence MB>
    auto
    read_some(MB buffers)
    {
        struct awaitable
        {
            stream* self_;
            MB buffers_;

            bool await_ready() const noexcept
            {
                if(buffer_empty(buffers_))
                    return true;
                auto* st = self_->state_.get();
                auto& side = st->sides[self_->index_];
                return st->closed || side.eof ||
                    !side.buf.empty();
            }

            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<> h,
                io_env const* env) noexcept
            {
                auto& side = self_->state_->sides[
                    self_->index_];
                side.pending_h = h;
                side.pending_ex = env->executor;
                return std::noop_coroutine();
            }

            io_result<std::size_t>
            await_resume()
            {
                if(buffer_empty(buffers_))
                    return {{}, 0};

                auto* st = self_->state_.get();
                auto& side = st->sides[
                    self_->index_];

                if(st->closed)
                    return {error::eof, 0};

                if(side.eof && side.buf.empty())
                    return {error::eof, 0};

                if(!side.eof)
                {
                    close_guard g{st};
                    auto ec = st->f.maybe_fail();
                    if(ec)
                        return {ec, 0};
                    g.disarm();
                }

                std::size_t const n = buffer_copy(
                    buffers_, make_buffer(side.buf),
                    side.max_read_size);
                side.buf.erase(0, n);
                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    /** Asynchronously write data to the stream.

        Transfers up to `buffer_size(buffers)` bytes to the
        peer's incoming buffer. If the peer is suspended in
        @ref read_some, it is resumed. Before every write,
        the attached @ref fuse is consulted to possibly inject
        an error. If the fuse fires, the peer is automatically
        closed. If the stream is closed, returns `error::eof`.
        The returned `std::size_t` is the number of bytes
        transferred.

        @param buffers The const buffer sequence containing
            data to write.

        @return An awaitable that await-returns `(error_code,std::size_t)`.

        @see fuse, close
    */
    template<ConstBufferSequence CB>
    auto
    write_some(CB buffers)
    {
        struct awaitable
        {
            stream* self_;
            CB buffers_;

            bool await_ready() const noexcept { return true; }

            void await_suspend(
                std::coroutine_handle<>,
                io_env const*) const noexcept
            {
            }

            io_result<std::size_t>
            await_resume()
            {
                std::size_t n = buffer_size(buffers_);
                if(n == 0)
                    return {{}, 0};

                auto* st = self_->state_.get();

                if(st->closed)
                    return {error::eof, 0};

                close_guard g{st};
                auto ec = st->f.maybe_fail();
                if(ec)
                    return {ec, 0};
                g.disarm();

                int peer = 1 - self_->index_;
                auto& side = st->sides[peer];

                std::size_t const old_size = side.buf.size();
                side.buf.resize(old_size + n);
                buffer_copy(make_buffer(
                    side.buf.data() + old_size, n),
                    buffers_, n);

                if(side.pending_h)
                {
                    auto h = side.pending_h;
                    side.pending_h = {};
                    auto ex = side.pending_ex;
                    side.pending_ex = {};
                    ex.post(h);
                }

                return {{}, n};
            }
        };
        return awaitable{this, buffers};
    }

    /** Inject data into this stream's peer for reading.

        Appends data directly to the peer's incoming buffer,
        bypassing the fuse. If the peer is suspended in
        @ref read_some, it is resumed. This is test setup,
        not an operation under test.

        @param sv The data to inject.

        @see make_stream_pair
    */
    void
    provide(std::string_view sv)
    {
        int peer = 1 - index_;
        auto& side = state_->sides[peer];
        side.buf.append(sv);
        if(side.pending_h)
        {
            auto h = side.pending_h;
            side.pending_h = {};
            auto ex = side.pending_ex;
            side.pending_ex = {};
            ex.post(h);
        }
    }

    /** Read from this stream and verify the content.

        Reads exactly `expected.size()` bytes from the stream
        and compares against the expected string. The read goes
        through the normal path including the fuse.

        @param expected The expected content.

        @return A pair of `(error_code, bool)`. The error_code
            is set if a read error occurs (e.g. fuse injection).
            The bool is true if the data matches.

        @see provide
    */
    std::pair<std::error_code, bool>
    expect(std::string_view expected)
    {
        std::error_code result;
        bool match = false;
        run_blocking()([](
            stream& self,
            std::string_view expected,
            std::error_code& result,
            bool& match) -> task<>
        {
            std::string buf(expected.size(), '\0');
            auto [ec, n] = co_await read(
                self, mutable_buffer(
                    buf.data(), buf.size()));
            if(ec)
            {
                result = ec;
                co_return;
            }
            match = (std::string_view(
                buf.data(), n) == expected);
        }(*this, expected, result, match));
        return {result, match};
    }

    /** Return the stream's pending read data.

        Returns a view of the data waiting to be read
        from this stream. This is a direct peek at the
        internal buffer, bypassing the fuse.

        @return A view of the pending data.

        @see provide, expect
    */
    std::string_view
    data() const noexcept
    {
        return state_->sides[index_].buf;
    }
};

/** Create a connected pair of test streams.

    Data written to one stream becomes readable on the other.
    If a coroutine calls @ref stream::read_some when no data
    is available, it suspends until the peer writes. Before
    every read or write, the @ref fuse is consulted to
    possibly inject an error for testing fault scenarios.
    When the fuse fires, the peer is automatically closed.

    @param f The fuse used to inject errors during operations.

    @return A pair of connected streams.

    @see stream, fuse
*/
inline std::pair<stream, stream>
make_stream_pair(fuse f = {})
{
    auto sp = std::make_shared<stream::state>(std::move(f));
    return {stream(sp, 0), stream(sp, 1)};
}

} // test
} // capy
} // boost

#endif
