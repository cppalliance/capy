//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_WRITE_NOW_HPP
#define BOOST_CAPY_IO_WRITE_NOW_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/consuming_buffers.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <coroutine>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/io_env.hpp>
#include <boost/capy/io_result.hpp>

#include <cstddef>
#include <exception>
#include <new>
#include <stop_token>
#include <utility>

#ifndef BOOST_CAPY_WRITE_NOW_WORKAROUND
# if defined(__GNUC__) && !defined(__clang__)
#  define BOOST_CAPY_WRITE_NOW_WORKAROUND 1
# else
#  define BOOST_CAPY_WRITE_NOW_WORKAROUND 0
# endif
#endif

namespace boost {
namespace capy {

/** Eagerly writes complete buffer sequences with frame caching.

    This class wraps a @ref WriteStream and provides an `operator()`
    that writes an entire buffer sequence, attempting to complete
    synchronously. If every `write_some` completes without suspending,
    the entire operation finishes in `await_ready` with no coroutine
    suspension.

    The class maintains a one-element coroutine frame cache. After
    the first call, subsequent calls reuse the cached frame memory,
    avoiding repeated allocation for the internal coroutine.

    @tparam Stream The stream type, must satisfy @ref WriteStream.

    @par Thread Safety
    Distinct objects: Safe.
    Shared objects: Unsafe.

    @par Preconditions
    Only one operation may be outstanding at a time. A new call to
    `operator()` must not be made until the previous operation has
    completed (i.e., the returned awaitable has been fully consumed).

    @par Example

    @code
    template< WriteStream Stream >
    task<> send_messages( Stream& stream )
    {
        write_now wn( stream );
        auto [ec1, n1] = co_await wn( make_buffer( "hello" ) );
        if( ec1 )
            detail::throw_system_error( ec1 );
        auto [ec2, n2] = co_await wn( make_buffer( "world" ) );
        if( ec2 )
            detail::throw_system_error( ec2 );
    }
    @endcode

    @see write, write_some, WriteStream, ConstBufferSequence
*/
template<class Stream>
    requires WriteStream<Stream>
class write_now
{
    Stream& stream_;
    void* cached_frame_ = nullptr;
    std::size_t cached_size_ = 0;

    struct [[nodiscard]] BOOST_CAPY_CORO_AWAIT_ELIDABLE
        op_type
    {
        struct promise_type
        {
            io_result<std::size_t> result_;
            std::exception_ptr ep_;
            std::coroutine_handle<> cont_{nullptr};
            io_env env_;
            bool done_ = false;

            op_type get_return_object()
            {
                return op_type{
                    std::coroutine_handle<
                        promise_type>::from_promise(*this)};
            }

            auto initial_suspend() noexcept
            {
#if BOOST_CAPY_WRITE_NOW_WORKAROUND
                return std::suspend_always{};
#else
                return std::suspend_never{};
#endif
            }

            auto final_suspend() noexcept
            {
                struct awaiter
                {
                    promise_type* p_;

                    bool await_ready() const noexcept
                    {
                        return false;
                    }

                    std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept
                    {
                        p_->done_ = true;
                        if(!p_->cont_)
                            return std::noop_coroutine();
                        return p_->cont_;
                    }

                    void await_resume() const noexcept
                    {
                    }
                };
                return awaiter{this};
            }

            void return_value(
                io_result<std::size_t> r) noexcept
            {
                result_ = r;
            }

            void unhandled_exception()
            {
                ep_ = std::current_exception();
            }

            std::suspend_always yield_value(int) noexcept
            {
                return {};
            }

            template<class A>
            auto await_transform(A&& a)
            {
                using decayed = std::decay_t<A>;
                if constexpr (IoAwaitable<decayed>)
                {
                    struct wrapper
                    {
                        decayed inner_;
                        promise_type* p_;

                        bool await_ready()
                        {
                            return inner_.await_ready();
                        }

                        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h)
                        {
                            return detail::call_await_suspend(
                                &inner_, h,
                                p_->env_);
                        }

                        decltype(auto) await_resume()
                        {
                            return inner_.await_resume();
                        }
                    };
                    return wrapper{
                        std::forward<A>(a), this};
                }
                else
                {
                    return std::forward<A>(a);
                }
            }

            static void*
            operator new(
                std::size_t size,
                write_now& self,
                auto&)
            {
                if(self.cached_frame_ &&
                    self.cached_size_ >= size)
                    return self.cached_frame_;
                void* p = ::operator new(size);
                if(self.cached_frame_)
                    ::operator delete(self.cached_frame_);
                self.cached_frame_ = p;
                self.cached_size_ = size;
                return p;
            }

            static void
            operator delete(void*, std::size_t) noexcept
            {
            }
        };

        std::coroutine_handle<promise_type> h_;

        ~op_type()
        {
            if(h_)
                h_.destroy();
        }

        op_type(op_type const&) = delete;
        op_type& operator=(op_type const&) = delete;

        op_type(op_type&& other) noexcept
            : h_(std::exchange(other.h_, nullptr))
        {
        }

        op_type& operator=(op_type&&) = delete;

        bool await_ready() const noexcept
        {
            return h_.promise().done_;
        }

        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<> cont,
            io_env const& env)
        {
            auto& p = h_.promise();
            p.cont_ = cont;
            p.env_ = env;
            return h_;
        }

        io_result<std::size_t> await_resume()
        {
            auto& p = h_.promise();
            if(p.ep_)
                std::rethrow_exception(p.ep_);
            return p.result_;
        }

    private:
        explicit op_type(
            std::coroutine_handle<promise_type> h)
            : h_(h)
        {
        }
    };

public:
    /** Destructor. Frees the cached coroutine frame. */
    ~write_now()
    {
        if(cached_frame_)
            ::operator delete(cached_frame_);
    }

    /** Construct from a stream reference.

        @param s The stream to write to. Must outlive this object.
    */
    explicit
    write_now(Stream& s) noexcept
        : stream_(s)
    {
    }

    write_now(write_now const&) = delete;
    write_now& operator=(write_now const&) = delete;

    /** Eagerly write the entire buffer sequence.

        Writes data to the stream by calling `write_some` repeatedly
        until the entire buffer sequence is written or an error
        occurs. The operation attempts to complete synchronously:
        if every `write_some` completes without suspending, the
        entire operation finishes in `await_ready`.

        When the fast path cannot complete, the coroutine suspends
        and continues asynchronously. The internal coroutine frame
        is cached and reused across calls.

        @param buffers The buffer sequence to write. Passed by
            value to ensure the sequence lives in the coroutine
            frame across suspension points.

        @return An awaitable yielding `(error_code,std::size_t)`.
            On success, `n` equals `buffer_size(buffers)`. On
            error, `n` is the number of bytes written before the
            error. Compare error codes to conditions:
            @li `cond::canceled` - Operation was cancelled
            @li `std::errc::broken_pipe` - Peer closed connection

        @par Example

        @code
        write_now wn( stream );
        auto [ec, n] = co_await wn( make_buffer( body ) );
        if( ec )
            detail::throw_system_error( ec );
        @endcode

        @see write, write_some, WriteStream
    */
// GCC falsely warns that the coroutine promise's
// placement operator new(size_t, write_now&, auto&)
// mismatches operator delete(void*, size_t). Per the
// standard, coroutine deallocation lookup is separate.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

#if BOOST_CAPY_WRITE_NOW_WORKAROUND
    template<ConstBufferSequence Buffers>
    op_type
    operator()(Buffers buffers)
    {
        std::size_t const total_size = buffer_size(buffers);
        std::size_t total_written = 0;
        consuming_buffers cb(buffers);
        while(total_written < total_size)
        {
            auto r =
                co_await stream_.write_some(cb);
            if(r.ec)
                co_return io_result<std::size_t>{
                    r.ec, total_written};
            cb.consume(r.t1);
            total_written += r.t1;
        }
        co_return io_result<std::size_t>{
            {}, total_written};
    }
#else
    template<ConstBufferSequence Buffers>
    op_type
    operator()(Buffers buffers)
    {
        std::size_t const total_size = buffer_size(buffers);
        std::size_t total_written = 0;

        // GCC ICE in expand_expr_real_1 (expr.cc:11376)
        // when consuming_buffers spans a co_yield, so
        // the GCC path uses a separate simple coroutine.
        consuming_buffers cb(buffers);
        while(total_written < total_size)
        {
            auto inner = stream_.write_some(cb);
            if(!inner.await_ready())
                break;
            auto r = inner.await_resume();
            if(r.ec)
                co_return io_result<std::size_t>{
                    r.ec, total_written};
            cb.consume(r.t1);
            total_written += r.t1;
        }

        if(total_written >= total_size)
            co_return io_result<std::size_t>{
                {}, total_written};

        co_yield 0;

        while(total_written < total_size)
        {
            auto r =
                co_await stream_.write_some(cb);
            if(r.ec)
                co_return io_result<std::size_t>{
                    r.ec, total_written};
            cb.consume(r.t1);
            total_written += r.t1;
        }
        co_return io_result<std::size_t>{
            {}, total_written};
    }
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
};

template<WriteStream S>
write_now(S&) -> write_now<S>;

} // namespace capy
} // namespace boost

#endif
