//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ANY_BUFFER_SINK_HPP
#define BOOST_CAPY_IO_ANY_BUFFER_SINK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/buffer_param.hpp>
#include <boost/capy/concept/buffer_sink.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/write_sink.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/io_task.hpp>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <new>
#include <stop_token>
#include <system_error>
#include <utility>

namespace boost {
namespace capy {

/** Type-erased wrapper for any BufferSink.

    This class provides type erasure for any type satisfying the
    @ref BufferSink concept, enabling runtime polymorphism for
    buffer sink operations. It uses cached awaitable storage to achieve
    zero steady-state allocation after construction.

    The wrapper also satisfies @ref WriteSink through templated
    @ref write methods. These methods copy data from the caller's
    buffers into the sink's internal storage, incurring one extra
    buffer copy compared to using @ref prepare and @ref commit
    directly.

    The wrapper supports two construction modes:
    - **Owning**: Pass by value to transfer ownership. The wrapper
      allocates storage and owns the sink.
    - **Reference**: Pass a pointer to wrap without ownership. The
      pointed-to sink must outlive this wrapper.

    @par Awaitable Preallocation
    The constructor preallocates storage for the type-erased awaitable.
    This reserves all virtual address space at server startup
    so memory usage can be measured up front, rather than
    allocating piecemeal as traffic arrives.

    @par Thread Safety
    Not thread-safe. Concurrent operations on the same wrapper
    are undefined behavior.

    @par Example
    @code
    // Owning - takes ownership of the sink
    any_buffer_sink abs(some_buffer_sink{args...});

    // Reference - wraps without ownership
    some_buffer_sink sink;
    any_buffer_sink abs(&sink);

    mutable_buffer arr[16];
    auto bufs = abs.prepare(arr);
    // Write data into bufs[0..bufs.size())
    auto [ec] = co_await abs.commit(bytes_written);
    auto [ec2] = co_await abs.commit_eof();
    @endcode

    @see any_buffer_source, BufferSink, WriteSink
*/
class any_buffer_sink
{
    struct vtable;
    struct awaitable_ops;

    template<BufferSink S>
    struct vtable_for_impl;

    void* sink_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_awaitable_ = nullptr;
    void* storage_ = nullptr;
    awaitable_ops const* active_ops_ = nullptr;

public:
    /** Destructor.

        Destroys the owned sink (if any) and releases the cached
        awaitable storage.
    */
    ~any_buffer_sink();

    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_buffer_sink() = default;

    /** Non-copyable.

        The awaitable cache is per-instance and cannot be shared.
    */
    any_buffer_sink(any_buffer_sink const&) = delete;
    any_buffer_sink& operator=(any_buffer_sink const&) = delete;

    /** Move constructor.

        Transfers ownership of the wrapped sink (if owned) and
        cached awaitable storage from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_buffer_sink(any_buffer_sink&& other) noexcept
        : sink_(std::exchange(other.sink_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_awaitable_(std::exchange(other.cached_awaitable_, nullptr))
        , storage_(std::exchange(other.storage_, nullptr))
        , active_ops_(std::exchange(other.active_ops_, nullptr))
    {
    }

    /** Move assignment operator.

        Destroys any owned sink and releases existing resources,
        then transfers ownership from `other`.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    any_buffer_sink&
    operator=(any_buffer_sink&& other) noexcept;

    /** Construct by taking ownership of a BufferSink.

        Allocates storage and moves the sink into this wrapper.
        The wrapper owns the sink and will destroy it.

        @param s The sink to take ownership of.
    */
    template<BufferSink S>
        requires (!std::same_as<std::decay_t<S>, any_buffer_sink>)
    any_buffer_sink(S s);

    /** Construct by wrapping a BufferSink without ownership.

        Wraps the given sink by pointer. The sink must remain
        valid for the lifetime of this wrapper.

        @param s Pointer to the sink to wrap.
    */
    template<BufferSink S>
    any_buffer_sink(S* s);

    /** Check if the wrapper contains a valid sink.

        @return `true` if wrapping a sink, `false` if default-constructed
            or moved-from.
    */
    bool
    has_value() const noexcept
    {
        return sink_ != nullptr;
    }

    /** Check if the wrapper contains a valid sink.

        @return `true` if wrapping a sink, `false` if default-constructed
            or moved-from.
    */
    explicit
    operator bool() const noexcept
    {
        return has_value();
    }

    /** Prepare writable buffers.

        Fills the provided span with mutable buffer descriptors
        pointing to the underlying sink's internal storage. This
        operation is synchronous.

        @param dest Span of mutable_buffer to fill.

        @return A span of filled buffers.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    std::span<mutable_buffer>
    prepare(std::span<mutable_buffer> dest);

    /** Commit bytes written to the prepared buffers.

        Commits `n` bytes written to the buffers returned by the
        most recent call to @ref prepare. The operation may trigger
        underlying I/O.

        @param n The number of bytes to commit.

        @return An awaitable yielding `(error_code)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    auto
    commit(std::size_t n);

    /** Commit bytes written with optional end-of-stream.

        Commits `n` bytes written to the buffers returned by the
        most recent call to @ref prepare. If `eof` is true, also
        signals end-of-stream.

        @param n The number of bytes to commit.
        @param eof If true, signals end-of-stream after committing.

        @return An awaitable yielding `(error_code)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    auto
    commit(std::size_t n, bool eof);

    /** Signal end-of-stream.

        Indicates that no more data will be written to the sink.
        The operation completes when the sink is finalized, or
        an error occurs.

        @return An awaitable yielding `(error_code)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    auto
    commit_eof();

    /** Write some data from a buffer sequence.

        Writes one or more bytes from the buffer sequence to the
        underlying sink. May consume less than the full sequence.

        @param buffers The buffer sequence to write.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    template<ConstBufferSequence CB>
    io_task<std::size_t>
    write_some(CB buffers);

    /** Write all data from a buffer sequence.

        Writes all data from the buffer sequence to the underlying
        sink. This method satisfies the @ref WriteSink concept.

        @param buffers The buffer sequence to write.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    template<ConstBufferSequence CB>
    io_task<std::size_t>
    write(CB buffers);

    /** Atomically write data and signal end-of-stream.

        Writes all data from the buffer sequence to the underlying
        sink and then signals end-of-stream.

        @param buffers The buffer sequence to write.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    template<ConstBufferSequence CB>
    io_task<std::size_t>
    write_eof(CB buffers);

    /** Signal end-of-stream.

        Indicates that no more data will be written to the sink.
        This method satisfies the @ref WriteSink concept.

        @return An awaitable yielding `(error_code)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    auto
    write_eof();

protected:
    /** Rebind to a new sink after move.

        Updates the internal pointer to reference a new sink object.
        Used by owning wrappers after move assignment when the owned
        object has moved to a new location.

        @param new_sink The new sink to bind to. Must be the same
            type as the original sink.

        @note Terminates if called with a sink of different type
            than the original.
    */
    template<BufferSink S>
    void
    rebind(S& new_sink) noexcept
    {
        if(vt_ != &vtable_for_impl<S>::value)
            std::terminate();
        sink_ = &new_sink;
    }
};

//----------------------------------------------------------

struct any_buffer_sink::awaitable_ops
{
    bool (*await_ready)(void*);
    coro (*await_suspend)(void*, coro, executor_ref, std::stop_token);
    io_result<> (*await_resume)(void*);
    void (*destroy)(void*) noexcept;
};

struct any_buffer_sink::vtable
{
    void (*destroy)(void*) noexcept;
    std::span<mutable_buffer> (*do_prepare)(
        void* sink,
        std::span<mutable_buffer> dest);
    std::size_t awaitable_size;
    std::size_t awaitable_align;
    awaitable_ops const* (*construct_commit_awaitable)(
        void* sink,
        void* storage,
        std::size_t n,
        bool eof);
    awaitable_ops const* (*construct_eof_awaitable)(
        void* sink,
        void* storage);
};

template<BufferSink S>
struct any_buffer_sink::vtable_for_impl
{
    using CommitAwaitable = decltype(std::declval<S&>().commit(
        std::size_t{}, false));
    using EofAwaitable = decltype(std::declval<S&>().commit_eof());

    static void
    do_destroy_impl(void* sink) noexcept
    {
        static_cast<S*>(sink)->~S();
    }

    static std::span<mutable_buffer>
    do_prepare_impl(
        void* sink,
        std::span<mutable_buffer> dest)
    {
        auto& s = *static_cast<S*>(sink);
        return s.prepare(dest);
    }

    static awaitable_ops const*
    construct_commit_awaitable_impl(
        void* sink,
        void* storage,
        std::size_t n,
        bool eof)
    {
        auto& s = *static_cast<S*>(sink);
        ::new(storage) CommitAwaitable(s.commit(n, eof));

        static constexpr awaitable_ops ops = {
            +[](void* p) {
                return static_cast<CommitAwaitable*>(p)->await_ready();
            },
            +[](void* p, coro h, executor_ref ex, std::stop_token token) {
                return detail::call_await_suspend(
                    static_cast<CommitAwaitable*>(p), h, ex, token);
            },
            +[](void* p) {
                return static_cast<CommitAwaitable*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<CommitAwaitable*>(p)->~CommitAwaitable();
            }
        };
        return &ops;
    }

    static awaitable_ops const*
    construct_eof_awaitable_impl(
        void* sink,
        void* storage)
    {
        auto& s = *static_cast<S*>(sink);
        ::new(storage) EofAwaitable(s.commit_eof());

        static constexpr awaitable_ops ops = {
            +[](void* p) {
                return static_cast<EofAwaitable*>(p)->await_ready();
            },
            +[](void* p, coro h, executor_ref ex, std::stop_token token) {
                return detail::call_await_suspend(
                    static_cast<EofAwaitable*>(p), h, ex, token);
            },
            +[](void* p) {
                return static_cast<EofAwaitable*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<EofAwaitable*>(p)->~EofAwaitable();
            }
        };
        return &ops;
    }

    static constexpr std::size_t max_awaitable_size =
        sizeof(CommitAwaitable) > sizeof(EofAwaitable)
            ? sizeof(CommitAwaitable)
            : sizeof(EofAwaitable);

    static constexpr std::size_t max_awaitable_align =
        alignof(CommitAwaitable) > alignof(EofAwaitable)
            ? alignof(CommitAwaitable)
            : alignof(EofAwaitable);

    static constexpr vtable value = {
        &do_destroy_impl,
        &do_prepare_impl,
        max_awaitable_size,
        max_awaitable_align,
        &construct_commit_awaitable_impl,
        &construct_eof_awaitable_impl
    };
};

//----------------------------------------------------------

inline
any_buffer_sink::~any_buffer_sink()
{
    if(storage_)
    {
        vt_->destroy(sink_);
        ::operator delete(storage_);
    }
    if(cached_awaitable_)
        ::operator delete(cached_awaitable_);
}

inline any_buffer_sink&
any_buffer_sink::operator=(any_buffer_sink&& other) noexcept
{
    if(this != &other)
    {
        if(storage_)
        {
            vt_->destroy(sink_);
            ::operator delete(storage_);
        }
        if(cached_awaitable_)
            ::operator delete(cached_awaitable_);
        sink_ = std::exchange(other.sink_, nullptr);
        vt_ = std::exchange(other.vt_, nullptr);
        cached_awaitable_ = std::exchange(other.cached_awaitable_, nullptr);
        storage_ = std::exchange(other.storage_, nullptr);
        active_ops_ = std::exchange(other.active_ops_, nullptr);
    }
    return *this;
}

template<BufferSink S>
    requires (!std::same_as<std::decay_t<S>, any_buffer_sink>)
any_buffer_sink::any_buffer_sink(S s)
    : vt_(&vtable_for_impl<S>::value)
{
    struct guard {
        any_buffer_sink* self;
        bool committed = false;
        ~guard() {
            if(!committed && self->storage_) {
                self->vt_->destroy(self->sink_);
                ::operator delete(self->storage_);
                self->storage_ = nullptr;
                self->sink_ = nullptr;
            }
        }
    } g{this};

    storage_ = ::operator new(sizeof(S));
    sink_ = ::new(storage_) S(std::move(s));

    // Preallocate the awaitable storage (sized for max of commit/eof)
    cached_awaitable_ = ::operator new(vt_->awaitable_size);

    g.committed = true;
}

template<BufferSink S>
any_buffer_sink::any_buffer_sink(S* s)
    : sink_(s)
    , vt_(&vtable_for_impl<S>::value)
{
    // Preallocate the awaitable storage (sized for max of commit/eof)
    cached_awaitable_ = ::operator new(vt_->awaitable_size);
}

//----------------------------------------------------------

inline std::span<mutable_buffer>
any_buffer_sink::prepare(std::span<mutable_buffer> dest)
{
    return vt_->do_prepare(sink_, dest);
}

inline auto
any_buffer_sink::commit(std::size_t n, bool eof)
{
    struct awaitable
    {
        any_buffer_sink* self_;
        std::size_t n_;
        bool eof_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            // Construct the underlying awaitable into cached storage
            self_->active_ops_ = self_->vt_->construct_commit_awaitable(
                self_->sink_,
                self_->cached_awaitable_,
                n_,
                eof_);

            // Check if underlying is immediately ready
            if(self_->active_ops_->await_ready(self_->cached_awaitable_))
                return h;

            // Forward to underlying awaitable
            return self_->active_ops_->await_suspend(
                self_->cached_awaitable_, h, ex, token);
        }

        io_result<>
        await_resume()
        {
            struct guard {
                any_buffer_sink* self;
                ~guard() {
                    self->active_ops_->destroy(self->cached_awaitable_);
                    self->active_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this, n, eof};
}

inline auto
any_buffer_sink::commit(std::size_t n)
{
    return commit(n, false);
}

inline auto
any_buffer_sink::commit_eof()
{
    struct awaitable
    {
        any_buffer_sink* self_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            // Construct the underlying awaitable into cached storage
            self_->active_ops_ = self_->vt_->construct_eof_awaitable(
                self_->sink_,
                self_->cached_awaitable_);

            // Check if underlying is immediately ready
            if(self_->active_ops_->await_ready(self_->cached_awaitable_))
                return h;

            // Forward to underlying awaitable
            return self_->active_ops_->await_suspend(
                self_->cached_awaitable_, h, ex, token);
        }

        io_result<>
        await_resume()
        {
            struct guard {
                any_buffer_sink* self;
                ~guard() {
                    self->active_ops_->destroy(self->cached_awaitable_);
                    self->active_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this};
}

//----------------------------------------------------------

template<ConstBufferSequence CB>
io_task<std::size_t>
any_buffer_sink::write_some(CB buffers)
{
    buffer_param<CB> bp(buffers);
    auto src = bp.data();
    if(src.empty())
        co_return {{}, 0};

    mutable_buffer arr[detail::max_iovec_];
    auto dst_bufs = prepare(arr);
    if(dst_bufs.empty())
    {
        auto [ec] = co_await commit(0);
        if(ec)
            co_return {ec, 0};
        // Retry after flush
        dst_bufs = prepare(arr);
        if(dst_bufs.empty())
            co_return {{}, 0};
    }

    auto n = buffer_copy(dst_bufs, src);
    auto [ec] = co_await commit(n);
    if(ec)
        co_return {ec, 0};
    co_return {{}, n};
}

template<ConstBufferSequence CB>
io_task<std::size_t>
any_buffer_sink::write(CB buffers)
{
    buffer_param<CB> bp(buffers);
    std::size_t total = 0;

    for(;;)
    {
        auto src = bp.data();
        if(src.empty())
            break;

        mutable_buffer arr[detail::max_iovec_];
        auto dst_bufs = prepare(arr);
        if(dst_bufs.empty())
        {
            auto [ec] = co_await commit(0);
            if(ec)
                co_return {ec, total};
            continue;
        }

        auto n = buffer_copy(dst_bufs, src);
        auto [ec] = co_await commit(n);
        if(ec)
            co_return {ec, total};
        bp.consume(n);
        total += n;
    }

    co_return {{}, total};
}

template<ConstBufferSequence CB>
io_task<std::size_t>
any_buffer_sink::write_eof(CB buffers)
{
    buffer_param<CB> bp(buffers);
    std::size_t total = 0;

    for(;;)
    {
        auto src = bp.data();
        if(src.empty())
            break;

        mutable_buffer arr[detail::max_iovec_];
        auto dst_bufs = prepare(arr);
        if(dst_bufs.empty())
        {
            auto [ec] = co_await commit(0);
            if(ec)
                co_return {ec, total};
            continue;
        }

        auto n = buffer_copy(dst_bufs, src);
        auto [ec] = co_await commit(n);
        if(ec)
            co_return {ec, total};
        bp.consume(n);
        total += n;
    }

    auto [ec] = co_await commit_eof();
    if(ec)
        co_return {ec, total};

    co_return {{}, total};
}

inline auto
any_buffer_sink::write_eof()
{
    return commit_eof();
}

//----------------------------------------------------------

static_assert(WriteSink<any_buffer_sink>);

} // namespace capy
} // namespace boost

#endif
