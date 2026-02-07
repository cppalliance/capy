//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_IO_ANY_WRITE_SINK_HPP
#define BOOST_CAPY_IO_ANY_WRITE_SINK_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/await_suspend_helper.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_array.hpp>
#include <boost/capy/buffers/buffer_param.hpp>
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
#include <span>
#include <stop_token>
#include <system_error>
#include <utility>

namespace boost {
namespace capy {

/** Type-erased wrapper for any WriteSink.

    This class provides type erasure for any type satisfying the
    @ref WriteSink concept, enabling runtime polymorphism for
    sink write operations. It uses cached awaitable storage to achieve
    zero steady-state allocation after construction.

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

    @par Immediate Completion
    Operations complete immediately without suspending when the
    buffer sequence is empty, or when the underlying sink's
    awaitable reports readiness via `await_ready`.

    @par Thread Safety
    Not thread-safe. Concurrent operations on the same wrapper
    are undefined behavior.

    @par Example
    @code
    // Owning - takes ownership of the sink
    any_write_sink ws(some_sink{args...});

    // Reference - wraps without ownership
    some_sink sink;
    any_write_sink ws(&sink);

    const_buffer buf(data, size);
    auto [ec, n] = co_await ws.write(std::span(&buf, 1));
    auto [ec2] = co_await ws.write_eof();
    @endcode

    @see any_write_stream, WriteSink
*/
class any_write_sink
{
    struct vtable;
    struct write_awaitable_ops;
    struct eof_awaitable_ops;

    template<WriteSink S>
    struct vtable_for_impl;

    void* sink_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_awaitable_ = nullptr;
    void* storage_ = nullptr;
    write_awaitable_ops const* active_write_ops_ = nullptr;
    eof_awaitable_ops const* active_eof_ops_ = nullptr;

public:
    /** Destructor.

        Destroys the owned sink (if any) and releases the cached
        awaitable storage.
    */
    ~any_write_sink();

    /** Default constructor.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_write_sink() = default;

    /** Non-copyable.

        The awaitable cache is per-instance and cannot be shared.
    */
    any_write_sink(any_write_sink const&) = delete;
    any_write_sink& operator=(any_write_sink const&) = delete;

    /** Move constructor.

        Transfers ownership of the wrapped sink (if owned) and
        cached awaitable storage from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_write_sink(any_write_sink&& other) noexcept
        : sink_(std::exchange(other.sink_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_awaitable_(std::exchange(other.cached_awaitable_, nullptr))
        , storage_(std::exchange(other.storage_, nullptr))
        , active_write_ops_(std::exchange(other.active_write_ops_, nullptr))
        , active_eof_ops_(std::exchange(other.active_eof_ops_, nullptr))
    {
    }

    /** Move assignment operator.

        Destroys any owned sink and releases existing resources,
        then transfers ownership from `other`.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    any_write_sink&
    operator=(any_write_sink&& other) noexcept;

    /** Construct by taking ownership of a WriteSink.

        Allocates storage and moves the sink into this wrapper.
        The wrapper owns the sink and will destroy it.

        @param s The sink to take ownership of.
    */
    template<WriteSink S>
        requires (!std::same_as<std::decay_t<S>, any_write_sink>)
    any_write_sink(S s);

    /** Construct by wrapping a WriteSink without ownership.

        Wraps the given sink by pointer. The sink must remain
        valid for the lifetime of this wrapper.

        @param s Pointer to the sink to wrap.
    */
    template<WriteSink S>
    any_write_sink(S* s);

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

    /** Initiate a partial write operation.

        Writes one or more bytes from the provided buffer sequence.
        May consume less than the full sequence.

        @param buffers The buffer sequence containing data to write.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Immediate Completion
        The operation completes immediately without suspending
        the calling coroutine when:
        @li The buffer sequence is empty, returning `{error_code{}, 0}`.
        @li The underlying sink's awaitable reports immediate
            readiness via `await_ready`.

        @note This is a partial operation and may not process the
        entire buffer sequence. Use @ref write for guaranteed
        complete transfer.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    template<ConstBufferSequence CB>
    auto
    write_some(CB buffers);

    /** Initiate a complete write operation.

        Writes data from the provided buffer sequence. The operation
        completes when all bytes have been consumed, or an error
        occurs. Forwards to the underlying sink's `write` operation,
        windowed through @ref buffer_param when the sequence exceeds
        the per-call buffer limit.

        @param buffers The buffer sequence containing data to write.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Immediate Completion
        The operation completes immediately without suspending
        the calling coroutine when:
        @li The buffer sequence is empty, returning `{error_code{}, 0}`.
        @li Every underlying `write` call completes
            immediately (the wrapped sink reports readiness
            via `await_ready` on each iteration).

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    template<ConstBufferSequence CB>
    io_task<std::size_t>
    write(CB buffers);

    /** Atomically write data and signal end-of-stream.

        Writes all data from the buffer sequence and then signals
        end-of-stream. The implementation decides how to partition
        the data across calls to the underlying sink's @ref write
        and `write_eof`. When the caller's buffer sequence is
        non-empty, the final call to the underlying sink is always
        `write_eof` with a non-empty buffer sequence. When the
        caller's buffer sequence is empty, only `write_eof()` with
        no data is called.

        @param buffers The buffer sequence containing data to write.

        @return An awaitable yielding `(error_code,std::size_t)`.

        @par Immediate Completion
        The operation completes immediately without suspending
        the calling coroutine when:
        @li The buffer sequence is empty. Only the @ref write_eof()
            call is performed.
        @li All underlying operations complete immediately (the
            wrapped sink reports readiness via `await_ready`).

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    template<ConstBufferSequence CB>
    io_task<std::size_t>
    write_eof(CB buffers);

    /** Signal end of data.

        Indicates that no more data will be written to the sink.
        The operation completes when the sink is finalized, or
        an error occurs.

        @return An awaitable yielding `(error_code)`.

        @par Immediate Completion
        The operation completes immediately without suspending
        the calling coroutine when the underlying sink's awaitable
        reports immediate readiness via `await_ready`.

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
    template<WriteSink S>
    void
    rebind(S& new_sink) noexcept
    {
        if(vt_ != &vtable_for_impl<S>::value)
            std::terminate();
        sink_ = &new_sink;
    }

private:
    auto
    write_some_(std::span<const_buffer const> buffers);

    auto
    write_(std::span<const_buffer const> buffers);

    auto
    write_eof_buffers_(std::span<const_buffer const> buffers);
};

//----------------------------------------------------------

struct any_write_sink::write_awaitable_ops
{
    bool (*await_ready)(void*);
    coro (*await_suspend)(void*, coro, executor_ref, std::stop_token);
    io_result<std::size_t> (*await_resume)(void*);
    void (*destroy)(void*) noexcept;
};

struct any_write_sink::eof_awaitable_ops
{
    bool (*await_ready)(void*);
    coro (*await_suspend)(void*, coro, executor_ref, std::stop_token);
    io_result<> (*await_resume)(void*);
    void (*destroy)(void*) noexcept;
};

struct any_write_sink::vtable
{
    write_awaitable_ops const* (*construct_write_some_awaitable)(
        void* sink,
        void* storage,
        std::span<const_buffer const> buffers);
    write_awaitable_ops const* (*construct_write_awaitable)(
        void* sink,
        void* storage,
        std::span<const_buffer const> buffers);
    write_awaitable_ops const* (*construct_write_eof_buffers_awaitable)(
        void* sink,
        void* storage,
        std::span<const_buffer const> buffers);
    eof_awaitable_ops const* (*construct_eof_awaitable)(
        void* sink,
        void* storage);
    std::size_t awaitable_size;
    std::size_t awaitable_align;
    void (*destroy)(void*) noexcept;
};

template<WriteSink S>
struct any_write_sink::vtable_for_impl
{
    using WriteSomeAwaitable = decltype(std::declval<S&>().write_some(
        std::span<const_buffer const>{}));
    using WriteAwaitable = decltype(std::declval<S&>().write(
        std::span<const_buffer const>{}));
    using WriteEofBuffersAwaitable = decltype(std::declval<S&>().write_eof(
        std::span<const_buffer const>{}));
    using EofAwaitable = decltype(std::declval<S&>().write_eof());

    static void
    do_destroy_impl(void* sink) noexcept
    {
        static_cast<S*>(sink)->~S();
    }

    static write_awaitable_ops const*
    construct_write_some_awaitable_impl(
        void* sink,
        void* storage,
        std::span<const_buffer const> buffers)
    {
        auto& s = *static_cast<S*>(sink);
        ::new(storage) WriteSomeAwaitable(s.write_some(buffers));

        static constexpr write_awaitable_ops ops = {
            +[](void* p) {
                return static_cast<WriteSomeAwaitable*>(p)->await_ready();
            },
            +[](void* p, coro h, executor_ref ex, std::stop_token token) {
                return detail::call_await_suspend(
                    static_cast<WriteSomeAwaitable*>(p), h, ex, token);
            },
            +[](void* p) {
                return static_cast<WriteSomeAwaitable*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<WriteSomeAwaitable*>(p)->~WriteSomeAwaitable();
            }
        };
        return &ops;
    }

    static write_awaitable_ops const*
    construct_write_awaitable_impl(
        void* sink,
        void* storage,
        std::span<const_buffer const> buffers)
    {
        auto& s = *static_cast<S*>(sink);
        ::new(storage) WriteAwaitable(s.write(buffers));

        static constexpr write_awaitable_ops ops = {
            +[](void* p) {
                return static_cast<WriteAwaitable*>(p)->await_ready();
            },
            +[](void* p, coro h, executor_ref ex, std::stop_token token) {
                return detail::call_await_suspend(
                    static_cast<WriteAwaitable*>(p), h, ex, token);
            },
            +[](void* p) {
                return static_cast<WriteAwaitable*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<WriteAwaitable*>(p)->~WriteAwaitable();
            }
        };
        return &ops;
    }

    static write_awaitable_ops const*
    construct_write_eof_buffers_awaitable_impl(
        void* sink,
        void* storage,
        std::span<const_buffer const> buffers)
    {
        auto& s = *static_cast<S*>(sink);
        ::new(storage) WriteEofBuffersAwaitable(s.write_eof(buffers));

        static constexpr write_awaitable_ops ops = {
            +[](void* p) {
                return static_cast<WriteEofBuffersAwaitable*>(p)->await_ready();
            },
            +[](void* p, coro h, executor_ref ex, std::stop_token token) {
                return detail::call_await_suspend(
                    static_cast<WriteEofBuffersAwaitable*>(p), h, ex, token);
            },
            +[](void* p) {
                return static_cast<WriteEofBuffersAwaitable*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<WriteEofBuffersAwaitable*>(p)->~WriteEofBuffersAwaitable();
            }
        };
        return &ops;
    }

    static eof_awaitable_ops const*
    construct_eof_awaitable_impl(
        void* sink,
        void* storage)
    {
        auto& s = *static_cast<S*>(sink);
        ::new(storage) EofAwaitable(s.write_eof());

        static constexpr eof_awaitable_ops ops = {
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

    static constexpr std::size_t max4(
        std::size_t a, std::size_t b,
        std::size_t c, std::size_t d) noexcept
    {
        std::size_t ab = a > b ? a : b;
        std::size_t cd = c > d ? c : d;
        return ab > cd ? ab : cd;
    }

    static constexpr std::size_t max_awaitable_size =
        max4(sizeof(WriteSomeAwaitable),
             sizeof(WriteAwaitable),
             sizeof(WriteEofBuffersAwaitable),
             sizeof(EofAwaitable));

    static constexpr std::size_t max_awaitable_align =
        max4(alignof(WriteSomeAwaitable),
             alignof(WriteAwaitable),
             alignof(WriteEofBuffersAwaitable),
             alignof(EofAwaitable));

    static constexpr vtable value = {
        &construct_write_some_awaitable_impl,
        &construct_write_awaitable_impl,
        &construct_write_eof_buffers_awaitable_impl,
        &construct_eof_awaitable_impl,
        max_awaitable_size,
        max_awaitable_align,
        &do_destroy_impl
    };
};

//----------------------------------------------------------

inline
any_write_sink::~any_write_sink()
{
    if(storage_)
    {
        vt_->destroy(sink_);
        ::operator delete(storage_);
    }
    if(cached_awaitable_)
    {
        if(active_write_ops_)
            active_write_ops_->destroy(cached_awaitable_);
        else if(active_eof_ops_)
            active_eof_ops_->destroy(cached_awaitable_);
        ::operator delete(cached_awaitable_);
    }
}

inline any_write_sink&
any_write_sink::operator=(any_write_sink&& other) noexcept
{
    if(this != &other)
    {
        if(storage_)
        {
            vt_->destroy(sink_);
            ::operator delete(storage_);
        }
        if(cached_awaitable_)
        {
            if(active_write_ops_)
                active_write_ops_->destroy(cached_awaitable_);
            else if(active_eof_ops_)
                active_eof_ops_->destroy(cached_awaitable_);
            ::operator delete(cached_awaitable_);
        }
        sink_ = std::exchange(other.sink_, nullptr);
        vt_ = std::exchange(other.vt_, nullptr);
        cached_awaitable_ = std::exchange(other.cached_awaitable_, nullptr);
        storage_ = std::exchange(other.storage_, nullptr);
        active_write_ops_ = std::exchange(other.active_write_ops_, nullptr);
        active_eof_ops_ = std::exchange(other.active_eof_ops_, nullptr);
    }
    return *this;
}

template<WriteSink S>
    requires (!std::same_as<std::decay_t<S>, any_write_sink>)
any_write_sink::any_write_sink(S s)
    : vt_(&vtable_for_impl<S>::value)
{
    struct guard {
        any_write_sink* self;
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

    // Preallocate the awaitable storage (sized for max of write/eof)
    cached_awaitable_ = ::operator new(vt_->awaitable_size);

    g.committed = true;
}

template<WriteSink S>
any_write_sink::any_write_sink(S* s)
    : sink_(s)
    , vt_(&vtable_for_impl<S>::value)
{
    // Preallocate the awaitable storage (sized for max of write/eof)
    cached_awaitable_ = ::operator new(vt_->awaitable_size);
}

//----------------------------------------------------------

inline auto
any_write_sink::write_some_(
    std::span<const_buffer const> buffers)
{
    struct awaitable
    {
        any_write_sink* self_;
        std::span<const_buffer const> buffers_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            self_->active_write_ops_ = self_->vt_->construct_write_some_awaitable(
                self_->sink_,
                self_->cached_awaitable_,
                buffers_);

            if(self_->active_write_ops_->await_ready(self_->cached_awaitable_))
                return h;

            return self_->active_write_ops_->await_suspend(
                self_->cached_awaitable_, h, ex, token);
        }

        io_result<std::size_t>
        await_resume()
        {
            struct guard {
                any_write_sink* self;
                ~guard() {
                    self->active_write_ops_->destroy(self->cached_awaitable_);
                    self->active_write_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_write_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this, buffers};
}

inline auto
any_write_sink::write_(
    std::span<const_buffer const> buffers)
{
    struct awaitable
    {
        any_write_sink* self_;
        std::span<const_buffer const> buffers_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            self_->active_write_ops_ = self_->vt_->construct_write_awaitable(
                self_->sink_,
                self_->cached_awaitable_,
                buffers_);

            if(self_->active_write_ops_->await_ready(self_->cached_awaitable_))
                return h;

            return self_->active_write_ops_->await_suspend(
                self_->cached_awaitable_, h, ex, token);
        }

        io_result<std::size_t>
        await_resume()
        {
            struct guard {
                any_write_sink* self;
                ~guard() {
                    self->active_write_ops_->destroy(self->cached_awaitable_);
                    self->active_write_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_write_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this, buffers};
}

inline auto
any_write_sink::write_eof()
{
    struct awaitable
    {
        any_write_sink* self_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            // Construct the underlying awaitable into cached storage
            self_->active_eof_ops_ = self_->vt_->construct_eof_awaitable(
                self_->sink_,
                self_->cached_awaitable_);

            // Check if underlying is immediately ready
            if(self_->active_eof_ops_->await_ready(self_->cached_awaitable_))
                return h;

            // Forward to underlying awaitable
            return self_->active_eof_ops_->await_suspend(
                self_->cached_awaitable_, h, ex, token);
        }

        io_result<>
        await_resume()
        {
            struct guard {
                any_write_sink* self;
                ~guard() {
                    self->active_eof_ops_->destroy(self->cached_awaitable_);
                    self->active_eof_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_eof_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this};
}

inline auto
any_write_sink::write_eof_buffers_(
    std::span<const_buffer const> buffers)
{
    struct awaitable
    {
        any_write_sink* self_;
        std::span<const_buffer const> buffers_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            self_->active_write_ops_ =
                self_->vt_->construct_write_eof_buffers_awaitable(
                    self_->sink_,
                    self_->cached_awaitable_,
                    buffers_);

            if(self_->active_write_ops_->await_ready(self_->cached_awaitable_))
                return h;

            return self_->active_write_ops_->await_suspend(
                self_->cached_awaitable_, h, ex, token);
        }

        io_result<std::size_t>
        await_resume()
        {
            struct guard {
                any_write_sink* self;
                ~guard() {
                    self->active_write_ops_->destroy(self->cached_awaitable_);
                    self->active_write_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_write_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this, buffers};
}

template<ConstBufferSequence CB>
auto
any_write_sink::write_some(CB buffers)
{
    struct awaitable
    {
        any_write_sink* self_;
        const_buffer_array<detail::max_iovec_> ba_;

        awaitable(
            any_write_sink* self,
            CB const& buffers)
            : self_(self)
            , ba_(buffers)
        {
        }

        bool
        await_ready() const noexcept
        {
            return ba_.to_span().empty();
        }

        coro
        await_suspend(coro h, executor_ref ex, std::stop_token token)
        {
            self_->active_write_ops_ = self_->vt_->construct_write_some_awaitable(
                self_->sink_,
                self_->cached_awaitable_,
                ba_.to_span());

            if(self_->active_write_ops_->await_ready(self_->cached_awaitable_))
                return h;

            return self_->active_write_ops_->await_suspend(
                self_->cached_awaitable_, h, ex, token);
        }

        io_result<std::size_t>
        await_resume()
        {
            if(ba_.to_span().empty())
                return {{}, 0};

            struct guard {
                any_write_sink* self;
                ~guard() {
                    self->active_write_ops_->destroy(self->cached_awaitable_);
                    self->active_write_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_write_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this, buffers};
}

template<ConstBufferSequence CB>
io_task<std::size_t>
any_write_sink::write(CB buffers)
{
    buffer_param<CB> bp(buffers);
    std::size_t total = 0;

    for(;;)
    {
        auto bufs = bp.data();
        if(bufs.empty())
            break;

        auto [ec, n] = co_await write_(bufs);
        total += n;
        if(ec)
            co_return {ec, total};
        bp.consume(n);
    }

    co_return {{}, total};
}

template<ConstBufferSequence CB>
io_task<std::size_t>
any_write_sink::write_eof(CB buffers)
{
    const_buffer_param<CB> bp(buffers);
    std::size_t total = 0;

    for(;;)
    {
        auto bufs = bp.data();
        if(bufs.empty())
        {
            auto [ec] = co_await write_eof();
            co_return {ec, total};
        }

        if(! bp.more())
        {
            // Last window — send atomically with EOF
            auto [ec, n] = co_await write_eof_buffers_(bufs);
            total += n;
            co_return {ec, total};
        }

        auto [ec, n] = co_await write_(bufs);
        total += n;
        if(ec)
            co_return {ec, total};
        bp.consume(n);
    }
}

} // namespace capy
} // namespace boost

#endif
