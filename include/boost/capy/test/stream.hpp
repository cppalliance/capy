//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
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
#include <boost/capy/continuation.hpp>
#include <coroutine>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/read.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/run_blocking.hpp>

#include <atomic>
#include <memory>
#include <new>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace boost {
namespace capy {
namespace test {

/** Suspends a reader until its paired end writes, or the shared fuse injects an error.

    Streams are created in pairs via @ref make_stream_pair.
    Data written to one end becomes available for reading on
    the other. If no data is available when @ref read_some
    is called, the calling coroutine suspends until the peer
    calls @ref write_some. The shared @ref fuse enables error
    injection at controlled points in both directions.

    When the fuse injects an error or throws on one end, the
    pair is automatically closed. Any suspended reader on
    either end is resumed with `error::eof`, and subsequent
    operations on both ends return `error::eof`. Calling
    @ref close on one end signals eof to the peer's reads
    after draining any buffered data, while the peer may
    still write.

    @par Thread Safety
    Single-threaded only. Both ends of the pair must be
    accessed from the same thread. Concurrent access is
    undefined behavior.

    @par Example
    @par !example example


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
        continuation pending_cont_;
        executor_ref pending_ex;
        // Points at the suspended reader's claim flag (owned by the
        // read awaitable). Lets a peer wake coordinate with a stop
        // callback so the parked read is resumed exactly once.
        std::atomic<bool>* pending_claimed = nullptr;
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

        // Resume a suspended reader on this side, if any. Claims the
        // reader's atomic so it is never double-resumed by a racing
        // stop callback; the loser of the race skips the post.
        static void wake(half& side)
        {
            if(! side.pending_cont_.h)
                return;
            if(! side.pending_claimed ||
                ! side.pending_claimed->exchange(
                    true, std::memory_order_acq_rel))
            {
                side.pending_ex.post(side.pending_cont_);
            }
            side.pending_cont_.h = {};
            side.pending_ex = {};
            side.pending_claimed = nullptr;
        }

        // Set closed and resume any suspended readers
        // with eof on both sides.
        void close()
        {
            closed = true;
            for(auto& side : sides)
                wake(side);
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
    /** Copy construction is disabled; a stream end is move-only.

        @param other The stream end that would be copied.
    */
    stream(stream const& other) = delete;

    /** Copy assignment is disabled; a stream end is move-only.

        @param other The stream end that would be assigned from.

        @return A reference to `*this`.
    */
    stream& operator=(stream const& other) = delete;

    /** Move constructor.

        @param other The stream end to move from.
    */
    stream(stream&& other) = default;

    /** Move assignment.

        @param other The stream end to move from.

        @return A reference to `*this`.
    */
    stream& operator=(stream&& other) = default;

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
        state::wake(side);
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
        If the fuse fires, the pair is automatically closed.
        If the stream is closed, returns `error::eof`.
        The returned `std::size_t` is the number of bytes
        transferred.

        @param buffers The mutable buffer sequence to receive data.

        @return An awaitable that await-returns `(error_code,std::size_t)`.

        @par Cancellation
        Cancellation applies only to a read that would otherwise suspend.
        If no data is available and the environment's stop token is
        requested, before or during the wait, the read resumes with
        `error::canceled`. A read that can complete immediately from
        buffered data is unaffected by the stop token.

        @see fuse, close
    */
    template<MutableBufferSequence MB>
    auto
    read_some(MB buffers)
    {
        // The read suspends when no data is available, parking its
        // continuation on the side until the peer writes/closes. To
        // support cancellation it follows the same pattern as
        // async_waker::wait_awaiter: a stop callback claims the resume
        // (racing the peer wake via an atomic) and posts the continuation
        // through the executor. Because it owns a std::atomic and a
        // std::stop_callback, the awaitable needs explicit move and
        // destruction (the task promise moves it into its
        // transform_awaiter before awaiting).
        struct awaitable
        {
            stream* self_;
            MB buffers_;

            // Declared before stop_cb_buf_: the stop callback reads
            // these, so they must outlive a blocking stop_cb_ destructor.
            continuation cont_;
            executor_ref ex_;
            half* side_ = nullptr;
            std::atomic<bool> claimed_{false};
            bool canceled_ = false;
            bool stop_cb_active_ = false;

            struct cancel_fn
            {
                awaitable* self_;

                void operator()() const noexcept
                {
                    if(! self_->claimed_.exchange(
                        true, std::memory_order_acq_rel))
                    {
                        self_->canceled_ = true;
                        self_->ex_.post(self_->cont_);
                    }
                }
            };

            using stop_cb_t = std::stop_callback<cancel_fn>;

            // Declared last: its destructor may block while the callback
            // accesses the members above. A union gives correct alignment
            // for stop_cb_t without an alignas specifier, which avoids
            // MSVC's C4324 padding warning on this function-local class
            // (the member-level pragma used by async_waker::wait_awaiter
            // does not suppress it here). Lifetime is managed manually:
            // placement new in await_suspend, explicit destruction once done.
            union { stop_cb_t stop_cb_; };

            awaitable(stream* self, MB buffers) noexcept
                : self_(self)
                , buffers_(buffers)
            {
            }

            /// @pre Not yet awaited (no active stop callback).
            awaitable(awaitable&& o) noexcept
                : self_(o.self_)
                , buffers_(o.buffers_)
                , cont_(o.cont_)
                , ex_(o.ex_)
                , side_(o.side_)
                , claimed_(o.claimed_.load(std::memory_order_relaxed))
                , canceled_(o.canceled_)
                , stop_cb_active_(std::exchange(o.stop_cb_active_, false))
            {
            }

            ~awaitable()
            {
                if(stop_cb_active_)
                    stop_cb_.~stop_cb_t();
                // Unlink from the side if still parked (e.g. the
                // coroutine was destroyed while suspended), so a later
                // peer wake does not dereference a freed claim flag.
                if(side_ && side_->pending_claimed == &claimed_)
                {
                    side_->pending_cont_.h = {};
                    side_->pending_ex = {};
                    side_->pending_claimed = nullptr;
                }
            }

            awaitable(awaitable const&) = delete;
            awaitable& operator=(awaitable const&) = delete;
            awaitable& operator=(awaitable&&) = delete;

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
                // Park the continuation, then register the stop callback.
                // If stop is already requested, the callback fires inline
                // during construction: it claims the resume and posts the
                // continuation through the executor (never a symmetric
                // self-transfer, which would leak this frame under
                // run_async). The parked read is then resumed with
                // error::canceled by the run loop.
                auto& side = self_->state_->sides[
                    self_->index_];
                cont_.h = h;
                ex_ = env->executor;
                side_ = &side;
                side.pending_cont_.h = h;
                side.pending_ex = env->executor;
                side.pending_claimed = &claimed_;

                ::new(static_cast<void*>(&stop_cb_)) stop_cb_t(
                    env->stop_token, cancel_fn{this});
                stop_cb_active_ = true;

                return std::noop_coroutine();
            }

            [[nodiscard]] io_result<std::size_t>
            await_resume()
            {
                if(stop_cb_active_)
                {
                    stop_cb_.~stop_cb_t();
                    stop_cb_active_ = false;
                }

                if(buffer_empty(buffers_))
                    return {std::error_code(), 0};

                if(canceled_)
                {
                    // The stop callback posted us but left the side
                    // untouched; unlink if a peer wake has not already.
                    if(side_ && side_->pending_claimed == &claimed_)
                    {
                        side_->pending_cont_.h = {};
                        side_->pending_ex = {};
                        side_->pending_claimed = nullptr;
                    }
                    return {error::canceled, 0};
                }

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
                return {std::error_code(), n};
            }
        };
        return awaitable{this, buffers};
    }

    /** Asynchronously write data to the stream.

        Transfers up to `buffer_size(buffers)` bytes to the
        peer's incoming buffer. If the peer is suspended in
        @ref read_some, it is resumed. Before every write,
        the attached @ref fuse is consulted to possibly inject
        an error. If the fuse fires, the pair is automatically
        closed. If the stream is closed, returns `error::eof`.
        The returned `std::size_t` is the number of bytes
        transferred.

        @param buffers The const buffer sequence containing
            data to write.

        @return An awaitable that await-returns `(error_code,std::size_t)`.

        @par Cancellation
        If the environment's stop token is requested, the write
        completes immediately with `error::canceled` and transfers no
        data. An empty buffer sequence is a no-op that completes
        successfully regardless of the stop token.

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
            bool canceled_ = false;

            bool await_ready() const noexcept { return false; }

            // The write completes synchronously; await_suspend is only
            // used to observe the environment's stop token. Returning
            // false means the coroutine does not actually suspend.
            bool
            await_suspend(
                std::coroutine_handle<>,
                io_env const* env) noexcept
            {
                canceled_ = env->stop_token.stop_requested();
                return false;
            }

            [[nodiscard]] io_result<std::size_t>
            await_resume()
            {
                std::size_t n = buffer_size(buffers_);
                if(n == 0)
                    return {std::error_code(), 0};

                if(canceled_)
                    return {error::canceled, 0};

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

                state::wake(side);

                return {std::error_code(), n};
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
        state::wake(side);
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
    When the fuse fires, the pair is automatically closed.

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
