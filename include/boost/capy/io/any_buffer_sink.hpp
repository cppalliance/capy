//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
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
#include <boost/capy/ex/io_env.hpp>
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

/** Type-erased wrapper for any BufferSink.

    This class provides type erasure for any type satisfying the
    @ref BufferSink concept, enabling runtime polymorphism for
    buffer sink operations. It uses cached awaitable storage to achieve
    zero steady-state allocation after construction.

    The wrapper exposes two interfaces for producing data:
    the @ref BufferSink interface (`prepare`, `commit`, `commit_eof`)
    and the @ref WriteSink interface (`write_some`, `write`,
    `write_eof`). Choose the interface that matches how your data
    is produced:

    @par Choosing an Interface

    Use the **BufferSink** interface when you are a generator that
    produces data into externally-provided buffers. The sink owns
    the memory; you call @ref prepare to obtain writable buffers,
    fill them, then call @ref commit or @ref commit_eof.

    Use the **WriteSink** interface when you already have buffers
    containing the data to write:
    - If the entire body is available up front, call
      @ref write_eof(buffers) to send everything atomically.
    - If data arrives incrementally, call @ref write or
      @ref write_some in a loop, then @ref write_eof() when done.
      Prefer `write` (complete) unless your streaming pattern
      benefits from partial writes via `write_some`.

    If the wrapped type only satisfies @ref BufferSink, the
    @ref WriteSink operations are provided automatically.

    @par Construction Modes

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

    // BufferSink interface: generate into callee-owned buffers
    mutable_buffer arr[16];
    auto bufs = abs.prepare(arr);
    // Write data into bufs[0..bufs.size())
    auto [ec] = co_await abs.commit(bytes_written);
    auto [ec2] = co_await abs.commit_eof(0);

    // WriteSink interface: send caller-owned buffers
    auto [ec3, n] = co_await abs.write(make_buffer("hello", 5));
    auto [ec4] = co_await abs.write_eof();

    // Or send everything at once
    auto [ec5, n2] = co_await abs.write_eof(
        make_buffer(body_data));
    @endcode

    @see any_buffer_source, BufferSink, WriteSink
*/
class any_buffer_sink
{
    struct vtable;
    struct awaitable_ops;
    struct write_awaitable_ops;

    template<BufferSink S>
    struct vtable_for_impl;

    // hot-path members first for cache locality
    void* sink_ = nullptr;
    vtable const* vt_ = nullptr;
    void* cached_awaitable_ = nullptr;
    awaitable_ops const* active_ops_ = nullptr;
    write_awaitable_ops const* active_write_ops_ = nullptr;
    void* storage_ = nullptr;

public:
    /** Destructor.

        Destroys the owned sink (if any) and releases the cached
        awaitable storage.
    */
    ~any_buffer_sink();

    /** Construct a default instance.

        Constructs an empty wrapper. Operations on a default-constructed
        wrapper result in undefined behavior.
    */
    any_buffer_sink() = default;

    /** Non-copyable.

        The awaitable cache is per-instance and cannot be shared.
    */
    any_buffer_sink(any_buffer_sink const&) = delete;
    any_buffer_sink& operator=(any_buffer_sink const&) = delete;

    /** Construct by moving.

        Transfers ownership of the wrapped sink (if owned) and
        cached awaitable storage from `other`. After the move, `other` is
        in a default-constructed state.

        @param other The wrapper to move from.
    */
    any_buffer_sink(any_buffer_sink&& other) noexcept
        : sink_(std::exchange(other.sink_, nullptr))
        , vt_(std::exchange(other.vt_, nullptr))
        , cached_awaitable_(std::exchange(other.cached_awaitable_, nullptr))
        , active_ops_(std::exchange(other.active_ops_, nullptr))
        , active_write_ops_(std::exchange(other.active_write_ops_, nullptr))
        , storage_(std::exchange(other.storage_, nullptr))
    {
    }

    /** Assign by moving.

        Destroys any owned sink and releases existing resources,
        then transfers ownership from `other`.

        @param other The wrapper to move from.
        @return Reference to this wrapper.
    */
    any_buffer_sink&
    operator=(any_buffer_sink&& other) noexcept;

    /** Construct by taking ownership of a BufferSink.

        Allocates storage and moves the sink into this wrapper.
        The wrapper owns the sink and will destroy it. If `S` also
        satisfies @ref WriteSink, native write operations are
        forwarded through the virtual boundary.

        @param s The sink to take ownership of.
    */
    template<BufferSink S>
        requires (!std::same_as<std::decay_t<S>, any_buffer_sink>)
    any_buffer_sink(S s);

    /** Construct by wrapping a BufferSink without ownership.

        Wraps the given sink by pointer. The sink must remain
        valid for the lifetime of this wrapper. If `S` also
        satisfies @ref WriteSink, native write operations are
        forwarded through the virtual boundary.

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

    /** Commit final bytes and signal end-of-stream.

        Commits `n` bytes written to the buffers returned by the
        most recent call to @ref prepare and finalizes the sink.
        After success, no further operations are permitted.

        @param n The number of bytes to commit.

        @return An awaitable yielding `(error_code)`.

        @par Preconditions
        The wrapper must contain a valid sink (`has_value() == true`).
    */
    auto
    commit_eof(std::size_t n);

    /** Write some data from a buffer sequence.

        Writes one or more bytes from the buffer sequence to the
        underlying sink. May consume less than the full sequence.

        When the wrapped type provides native @ref WriteSink support,
        the operation forwards directly. Otherwise it is synthesized
        from @ref prepare and @ref commit with a buffer copy.

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

        When the wrapped type provides native @ref WriteSink support,
        each window is forwarded directly. Otherwise the data is
        copied into the sink via @ref prepare and @ref commit.

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

        When the wrapped type provides native @ref WriteSink support,
        the final window is sent atomically via the underlying
        `write_eof(buffers)`. Otherwise the data is synthesized
        through @ref prepare, @ref commit, and @ref commit_eof.

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

        When the wrapped type provides native @ref WriteSink support,
        the underlying `write_eof()` is called. Otherwise the
        operation is implemented as `commit_eof(0)`.

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

private:
    /** Forward a partial write through the vtable.

        Constructs the underlying `write_some` awaitable in
        cached storage and returns a type-erased awaitable.
    */
    auto
    write_some_(std::span<const_buffer const> buffers);

    /** Forward a complete write through the vtable.

        Constructs the underlying `write` awaitable in
        cached storage and returns a type-erased awaitable.
    */
    auto
    write_(std::span<const_buffer const> buffers);

    /** Forward an atomic write-with-EOF through the vtable.

        Constructs the underlying `write_eof(buffers)` awaitable
        in cached storage and returns a type-erased awaitable.
    */
    auto
    write_eof_buffers_(std::span<const_buffer const> buffers);
};

//----------------------------------------------------------

/** Type-erased ops for awaitables yielding `io_result<>`. */
struct any_buffer_sink::awaitable_ops
{
    bool (*await_ready)(void*);
    std::coroutine_handle<> (*await_suspend)(void*, std::coroutine_handle<>, io_env const*);
    io_result<> (*await_resume)(void*);
    void (*destroy)(void*) noexcept;
};

/** Type-erased ops for awaitables yielding `io_result<std::size_t>`. */
struct any_buffer_sink::write_awaitable_ops
{
    bool (*await_ready)(void*);
    std::coroutine_handle<> (*await_suspend)(void*, std::coroutine_handle<>, io_env const*);
    io_result<std::size_t> (*await_resume)(void*);
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
        std::size_t n);
    awaitable_ops const* (*construct_commit_eof_awaitable)(
        void* sink,
        void* storage,
        std::size_t n);

    // WriteSink forwarding (null when wrapped type is BufferSink-only)
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
    awaitable_ops const* (*construct_write_eof_awaitable)(
        void* sink,
        void* storage);
};

template<BufferSink S>
struct any_buffer_sink::vtable_for_impl
{
    using CommitAwaitable = decltype(std::declval<S&>().commit(
        std::size_t{}));
    using CommitEofAwaitable = decltype(std::declval<S&>().commit_eof(
        std::size_t{}));

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
        std::size_t n)
    {
        auto& s = *static_cast<S*>(sink);
        ::new(storage) CommitAwaitable(s.commit(n));

        static constexpr awaitable_ops ops = {
            +[](void* p) {
                return static_cast<CommitAwaitable*>(p)->await_ready();
            },
            +[](void* p, std::coroutine_handle<> h, io_env const* env) {
                return detail::call_await_suspend(
                    static_cast<CommitAwaitable*>(p), h, env);
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
    construct_commit_eof_awaitable_impl(
        void* sink,
        void* storage,
        std::size_t n)
    {
        auto& s = *static_cast<S*>(sink);
        ::new(storage) CommitEofAwaitable(s.commit_eof(n));

        static constexpr awaitable_ops ops = {
            +[](void* p) {
                return static_cast<CommitEofAwaitable*>(p)->await_ready();
            },
            +[](void* p, std::coroutine_handle<> h, io_env const* env) {
                return detail::call_await_suspend(
                    static_cast<CommitEofAwaitable*>(p), h, env);
            },
            +[](void* p) {
                return static_cast<CommitEofAwaitable*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<CommitEofAwaitable*>(p)->~CommitEofAwaitable();
            }
        };
        return &ops;
    }

    //------------------------------------------------------
    // WriteSink forwarding (only instantiated when WriteSink<S>)

    static write_awaitable_ops const*
    construct_write_some_awaitable_impl(
        void* sink,
        void* storage,
        std::span<const_buffer const> buffers)
        requires WriteSink<S>
    {
        using Aw = decltype(std::declval<S&>().write_some(
            std::span<const_buffer const>{}));
        auto& s = *static_cast<S*>(sink);
        ::new(storage) Aw(s.write_some(buffers));

        static constexpr write_awaitable_ops ops = {
            +[](void* p) {
                return static_cast<Aw*>(p)->await_ready();
            },
            +[](void* p, std::coroutine_handle<> h, io_env const* env) {
                return detail::call_await_suspend(
                    static_cast<Aw*>(p), h, env);
            },
            +[](void* p) {
                return static_cast<Aw*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<Aw*>(p)->~Aw();
            }
        };
        return &ops;
    }

    static write_awaitable_ops const*
    construct_write_awaitable_impl(
        void* sink,
        void* storage,
        std::span<const_buffer const> buffers)
        requires WriteSink<S>
    {
        using Aw = decltype(std::declval<S&>().write(
            std::span<const_buffer const>{}));
        auto& s = *static_cast<S*>(sink);
        ::new(storage) Aw(s.write(buffers));

        static constexpr write_awaitable_ops ops = {
            +[](void* p) {
                return static_cast<Aw*>(p)->await_ready();
            },
            +[](void* p, std::coroutine_handle<> h, io_env const* env) {
                return detail::call_await_suspend(
                    static_cast<Aw*>(p), h, env);
            },
            +[](void* p) {
                return static_cast<Aw*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<Aw*>(p)->~Aw();
            }
        };
        return &ops;
    }

    static write_awaitable_ops const*
    construct_write_eof_buffers_awaitable_impl(
        void* sink,
        void* storage,
        std::span<const_buffer const> buffers)
        requires WriteSink<S>
    {
        using Aw = decltype(std::declval<S&>().write_eof(
            std::span<const_buffer const>{}));
        auto& s = *static_cast<S*>(sink);
        ::new(storage) Aw(s.write_eof(buffers));

        static constexpr write_awaitable_ops ops = {
            +[](void* p) {
                return static_cast<Aw*>(p)->await_ready();
            },
            +[](void* p, std::coroutine_handle<> h, io_env const* env) {
                return detail::call_await_suspend(
                    static_cast<Aw*>(p), h, env);
            },
            +[](void* p) {
                return static_cast<Aw*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<Aw*>(p)->~Aw();
            }
        };
        return &ops;
    }

    static awaitable_ops const*
    construct_write_eof_awaitable_impl(
        void* sink,
        void* storage)
        requires WriteSink<S>
    {
        using Aw = decltype(std::declval<S&>().write_eof());
        auto& s = *static_cast<S*>(sink);
        ::new(storage) Aw(s.write_eof());

        static constexpr awaitable_ops ops = {
            +[](void* p) {
                return static_cast<Aw*>(p)->await_ready();
            },
            +[](void* p, std::coroutine_handle<> h, io_env const* env) {
                return detail::call_await_suspend(
                    static_cast<Aw*>(p), h, env);
            },
            +[](void* p) {
                return static_cast<Aw*>(p)->await_resume();
            },
            +[](void* p) noexcept {
                static_cast<Aw*>(p)->~Aw();
            }
        };
        return &ops;
    }

    //------------------------------------------------------

    static consteval std::size_t
    compute_max_size() noexcept
    {
        std::size_t s = sizeof(CommitAwaitable) > sizeof(CommitEofAwaitable)
            ? sizeof(CommitAwaitable)
            : sizeof(CommitEofAwaitable);
        if constexpr (WriteSink<S>)
        {
            using WS = decltype(std::declval<S&>().write_some(
                std::span<const_buffer const>{}));
            using W = decltype(std::declval<S&>().write(
                std::span<const_buffer const>{}));
            using WEB = decltype(std::declval<S&>().write_eof(
                std::span<const_buffer const>{}));
            using WE = decltype(std::declval<S&>().write_eof());

            if(sizeof(WS) > s) s = sizeof(WS);
            if(sizeof(W) > s) s = sizeof(W);
            if(sizeof(WEB) > s) s = sizeof(WEB);
            if(sizeof(WE) > s) s = sizeof(WE);
        }
        return s;
    }

    static consteval std::size_t
    compute_max_align() noexcept
    {
        std::size_t a = alignof(CommitAwaitable) > alignof(CommitEofAwaitable)
            ? alignof(CommitAwaitable)
            : alignof(CommitEofAwaitable);
        if constexpr (WriteSink<S>)
        {
            using WS = decltype(std::declval<S&>().write_some(
                std::span<const_buffer const>{}));
            using W = decltype(std::declval<S&>().write(
                std::span<const_buffer const>{}));
            using WEB = decltype(std::declval<S&>().write_eof(
                std::span<const_buffer const>{}));
            using WE = decltype(std::declval<S&>().write_eof());

            if(alignof(WS) > a) a = alignof(WS);
            if(alignof(W) > a) a = alignof(W);
            if(alignof(WEB) > a) a = alignof(WEB);
            if(alignof(WE) > a) a = alignof(WE);
        }
        return a;
    }

    static consteval vtable
    make_vtable() noexcept
    {
        vtable v{};
        v.destroy = &do_destroy_impl;
        v.do_prepare = &do_prepare_impl;
        v.awaitable_size = compute_max_size();
        v.awaitable_align = compute_max_align();
        v.construct_commit_awaitable = &construct_commit_awaitable_impl;
        v.construct_commit_eof_awaitable = &construct_commit_eof_awaitable_impl;
        v.construct_write_some_awaitable = nullptr;
        v.construct_write_awaitable = nullptr;
        v.construct_write_eof_buffers_awaitable = nullptr;
        v.construct_write_eof_awaitable = nullptr;

        if constexpr (WriteSink<S>)
        {
            v.construct_write_some_awaitable =
                &construct_write_some_awaitable_impl;
            v.construct_write_awaitable =
                &construct_write_awaitable_impl;
            v.construct_write_eof_buffers_awaitable =
                &construct_write_eof_buffers_awaitable_impl;
            v.construct_write_eof_awaitable =
                &construct_write_eof_awaitable_impl;
        }
        return v;
    }

    static constexpr vtable value = make_vtable();
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
        active_write_ops_ = std::exchange(other.active_write_ops_, nullptr);
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

    cached_awaitable_ = ::operator new(vt_->awaitable_size);

    g.committed = true;
}

template<BufferSink S>
any_buffer_sink::any_buffer_sink(S* s)
    : sink_(s)
    , vt_(&vtable_for_impl<S>::value)
{
    cached_awaitable_ = ::operator new(vt_->awaitable_size);
}

//----------------------------------------------------------

inline std::span<mutable_buffer>
any_buffer_sink::prepare(std::span<mutable_buffer> dest)
{
    return vt_->do_prepare(sink_, dest);
}

inline auto
any_buffer_sink::commit(std::size_t n)
{
    struct awaitable
    {
        any_buffer_sink* self_;
        std::size_t n_;

        bool
        await_ready()
        {
            self_->active_ops_ = self_->vt_->construct_commit_awaitable(
                self_->sink_,
                self_->cached_awaitable_,
                n_);
            return self_->active_ops_->await_ready(self_->cached_awaitable_);
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            return self_->active_ops_->await_suspend(
                self_->cached_awaitable_, h, env);
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
    return awaitable{this, n};
}

inline auto
any_buffer_sink::commit_eof(std::size_t n)
{
    struct awaitable
    {
        any_buffer_sink* self_;
        std::size_t n_;

        bool
        await_ready()
        {
            self_->active_ops_ = self_->vt_->construct_commit_eof_awaitable(
                self_->sink_,
                self_->cached_awaitable_,
                n_);
            return self_->active_ops_->await_ready(self_->cached_awaitable_);
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            return self_->active_ops_->await_suspend(
                self_->cached_awaitable_, h, env);
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
    return awaitable{this, n};
}

//----------------------------------------------------------
// Private helpers for native WriteSink forwarding

inline auto
any_buffer_sink::write_some_(
    std::span<const_buffer const> buffers)
{
    struct awaitable
    {
        any_buffer_sink* self_;
        std::span<const_buffer const> buffers_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            self_->active_write_ops_ =
                self_->vt_->construct_write_some_awaitable(
                    self_->sink_,
                    self_->cached_awaitable_,
                    buffers_);

            if(self_->active_write_ops_->await_ready(
                self_->cached_awaitable_))
                return h;

            return self_->active_write_ops_->await_suspend(
                self_->cached_awaitable_, h, env);
        }

        io_result<std::size_t>
        await_resume()
        {
            struct guard {
                any_buffer_sink* self;
                ~guard() {
                    self->active_write_ops_->destroy(
                        self->cached_awaitable_);
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
any_buffer_sink::write_(
    std::span<const_buffer const> buffers)
{
    struct awaitable
    {
        any_buffer_sink* self_;
        std::span<const_buffer const> buffers_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            self_->active_write_ops_ =
                self_->vt_->construct_write_awaitable(
                    self_->sink_,
                    self_->cached_awaitable_,
                    buffers_);

            if(self_->active_write_ops_->await_ready(
                self_->cached_awaitable_))
                return h;

            return self_->active_write_ops_->await_suspend(
                self_->cached_awaitable_, h, env);
        }

        io_result<std::size_t>
        await_resume()
        {
            struct guard {
                any_buffer_sink* self;
                ~guard() {
                    self->active_write_ops_->destroy(
                        self->cached_awaitable_);
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
any_buffer_sink::write_eof_buffers_(
    std::span<const_buffer const> buffers)
{
    struct awaitable
    {
        any_buffer_sink* self_;
        std::span<const_buffer const> buffers_;

        bool
        await_ready() const noexcept
        {
            return false;
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            self_->active_write_ops_ =
                self_->vt_->construct_write_eof_buffers_awaitable(
                    self_->sink_,
                    self_->cached_awaitable_,
                    buffers_);

            if(self_->active_write_ops_->await_ready(
                self_->cached_awaitable_))
                return h;

            return self_->active_write_ops_->await_suspend(
                self_->cached_awaitable_, h, env);
        }

        io_result<std::size_t>
        await_resume()
        {
            struct guard {
                any_buffer_sink* self;
                ~guard() {
                    self->active_write_ops_->destroy(
                        self->cached_awaitable_);
                    self->active_write_ops_ = nullptr;
                }
            } g{self_};
            return self_->active_write_ops_->await_resume(
                self_->cached_awaitable_);
        }
    };
    return awaitable{this, buffers};
}

//----------------------------------------------------------
// Public WriteSink methods

template<ConstBufferSequence CB>
io_task<std::size_t>
any_buffer_sink::write_some(CB buffers)
{
    buffer_param<CB> bp(buffers);
    auto src = bp.data();
    if(src.empty())
        co_return {{}, 0};

    // Native WriteSink path
    if(vt_->construct_write_some_awaitable)
        co_return co_await write_some_(src);

    // Synthesized path: prepare + buffer_copy + commit
    mutable_buffer arr[detail::max_iovec_];
    auto dst_bufs = prepare(arr);
    if(dst_bufs.empty())
    {
        auto [ec] = co_await commit(0);
        if(ec)
            co_return {ec, 0};
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

    // Native WriteSink path
    if(vt_->construct_write_awaitable)
    {
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

    // Synthesized path: prepare + buffer_copy + commit
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

inline auto
any_buffer_sink::write_eof()
{
    struct awaitable
    {
        any_buffer_sink* self_;

        bool
        await_ready()
        {
            if(self_->vt_->construct_write_eof_awaitable)
            {
                // Native WriteSink: forward to underlying write_eof()
                self_->active_ops_ =
                    self_->vt_->construct_write_eof_awaitable(
                        self_->sink_,
                        self_->cached_awaitable_);
            }
            else
            {
                // Synthesized: commit_eof(0)
                self_->active_ops_ =
                    self_->vt_->construct_commit_eof_awaitable(
                        self_->sink_,
                        self_->cached_awaitable_,
                        0);
            }
            return self_->active_ops_->await_ready(
                self_->cached_awaitable_);
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h, io_env const* env)
        {
            return self_->active_ops_->await_suspend(
                self_->cached_awaitable_, h, env);
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

template<ConstBufferSequence CB>
io_task<std::size_t>
any_buffer_sink::write_eof(CB buffers)
{
    // Native WriteSink path
    if(vt_->construct_write_eof_buffers_awaitable)
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

            if(!bp.more())
            {
                // Last window: send atomically with EOF
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

    // Synthesized path: prepare + buffer_copy + commit + commit_eof
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

    auto [ec] = co_await commit_eof(0);
    if(ec)
        co_return {ec, total};

    co_return {{}, total};
}

//----------------------------------------------------------

static_assert(BufferSink<any_buffer_sink>);
static_assert(WriteSink<any_buffer_sink>);

} // namespace capy
} // namespace boost

#endif
