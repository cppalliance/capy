//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/9.design/9n.WhyNotCobaltConcepts.adoc.

// Fragments deliberately leave results and bindings unused; the pages
// explain the values in prose instead.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-function"
// gcc 15 with sanitizers misattributes coroutine frame delete paths
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
#if defined(_MSC_VER)
#pragma warning(disable: 4834) // discarding [[nodiscard]] return value
#pragma warning(disable: 4189) // local variable initialized but not referenced
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4101) // unreferenced local variable
#pragma warning(disable: 4456) // declaration hides previous local declaration
#pragma warning(disable: 4457) // declaration hides function parameter
#pragma warning(disable: 4458) // declaration hides class member
#pragma warning(disable: 4459) // declaration hides global declaration
#endif

#include <boost/capy/buffers.hpp>
#include <boost/capy/concept/buffer_archetype.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/io_runnable.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/io/any_write_stream.hpp>
#include <boost/capy/task.hpp>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <new>
#include <system_error>
#include <type_traits>
#include <utility>

namespace capy = boost::capy;

namespace {

// tag::capy_signature[]
// Capy
capy::task<> my_algo(capy::any_write_stream& stream);
// end::capy_signature[]

// The concept definitions the page reproduces must stay in sync with
// the shipped ones; local mirrors compile them and the static_asserts
// below compare them against the real concepts.
namespace task_requirements {

using boost::capy::io_env;

// tag::io_awaitable_concept[]
template<typename A>
concept IoAwaitable =
    requires(A a, std::coroutine_handle<> h, io_env const* env)
    {
        a.await_suspend(h, env);
    };
// end::io_awaitable_concept[]

// tag::io_runnable_concept[]
template<typename T>
concept IoRunnable =
    IoAwaitable<T> &&
    requires { typename T::promise_type; } &&
    requires(T& t, T const& ct, typename T::promise_type const& cp,
             typename T::promise_type& p)
    {
        { ct.handle() } noexcept
            -> std::same_as<std::coroutine_handle<typename T::promise_type>>;
        { cp.exception() } noexcept -> std::same_as<std::exception_ptr>;
        { t.release() } noexcept;
        { p.set_continuation(std::coroutine_handle<>{}) } noexcept;
        { p.set_environment(static_cast<io_env const*>(nullptr)) } noexcept;
    } &&
    (std::is_void_v<decltype(std::declval<T&>().await_resume())> ||
     requires(typename T::promise_type& p) { p.result(); });
// end::io_runnable_concept[]

static_assert(IoAwaitable<capy::task<>> == capy::IoAwaitable<capy::task<>>);
static_assert(IoRunnable<capy::task<>> == capy::IoRunnable<capy::task<>>);
static_assert(IoRunnable<capy::task<int>> == capy::IoRunnable<capy::task<int>>);

} // namespace task_requirements

namespace context_propagation {

using boost::capy::io_env;

// Shows the protocol signature; the declaration is the fragment.
struct child_operation
{
    // tag::await_suspend_env[]
    auto await_suspend(std::coroutine_handle<> h, io_env const* env);
    // end::await_suspend_env[]
};

} // namespace context_propagation

namespace concept_short {

using boost::capy::const_buffer_archetype;
using boost::capy::IoAwaitable;

// tag::write_stream_concept_short[]
template<typename T>
concept WriteStream =
    requires(T& stream, const_buffer_archetype buffers)
    {
        { stream.write_some(buffers) } -> IoAwaitable;
        // ...
    };
// end::write_stream_concept_short[]

static_assert(WriteStream<capy::any_write_stream>);

} // namespace concept_short

// tag::semantics_quote[]
// From capy/concept/write_stream.hpp

// Semantic Requirements:
//
// Attempts to write up to buffer_size( buffers ) bytes from
// the buffer sequence to the stream.
//
// If buffer_size( buffers ) > 0:
//
//   If !ec, then n >= 1 && n <= buffer_size( buffers ).
//     n bytes were written from the buffer sequence.
//   If ec, then n >= 0 && n < buffer_size( buffers ).
//     n is the number of bytes written before the I/O
//     condition arose.
//
// Equivalently, n == buffer_size( buffers ) implies !ec: a
// completion that writes the entire buffer sequence is a success,
// even when the underlying operation also signals a condition. That
// condition is reported on a subsequent write. This lets generic
// composition algorithms such as when_all and when_any distinguish
// a completed transfer from a failure.
//
// If buffer_empty( buffers ) is true, n is 0. The empty
// buffer is not itself a cause for error, but ec may reflect
// the state of the stream.
//
// Buffers in the sequence are consumed in order.
//
// Buffer Lifetime:
//
// The caller must ensure that the memory referenced by buffers
// remains valid until the co_await expression returns.
// end::semantics_quote[]

// tag::lifetime_warning[]
// Warning: Pass buffer sequences by value. A by-value parameter
// is copied into the coroutine frame (or the awaitable's state),
// so the returned awaitable is self-contained and may be stored,
// moved across threads, or wrapped into a sender without lifetime
// concerns. A by-const-reference parameter binds to caller storage
// and is only safe when the awaitable is consumed immediately by
// co_await in the same scope; storing such an awaitable produces
// a dangling reference.
// end::lifetime_warning[]

namespace awaitable_storage {

using boost::capy::ConstBufferSequence;
using boost::capy::WriteStream;

// Scaffolding mirror of the real class shape so that the tagged
// regions, quoted from capy/io/any_write_stream.hpp, parse exactly
// as the page shows them.
class any_write_stream
{
    struct vtable
    {
        std::size_t awaitable_size;
    };

    template<WriteStream S>
    struct vtable_for_impl
    {
        static constexpr vtable value{sizeof(S)};
    };

    vtable const* vt_ = nullptr;
    void* storage_ = nullptr;
    void* stream_ = nullptr;
    void* cached_awaitable_ = nullptr;

public:
    template<WriteStream S>
        requires (!std::same_as<std::decay_t<S>, any_write_stream>)
    any_write_stream(S s);

    template<ConstBufferSequence CB>
    auto write_some(CB buffers);
};

// tag::any_write_some_template[]
template<ConstBufferSequence CB>
auto
any_write_stream::write_some(CB buffers)
{
    // ...
}
// end::any_write_some_template[]

// tag::ctor_prealloc[]
// capy/io/any_write_stream.hpp
template<WriteStream S>
    requires (!std::same_as<std::decay_t<S>, any_write_stream>)
any_write_stream::any_write_stream(S s)
    : vt_(&vtable_for_impl<S>::value)
{
    // ...
    storage_ = ::operator new(sizeof(S));
    stream_ = ::new(storage_) S(std::move(s));

    // Preallocate the awaitable storage
    cached_awaitable_ = ::operator new(vt_->awaitable_size);
    // ...
}
// end::ctor_prealloc[]

} // namespace awaitable_storage

namespace concept_full {

using boost::capy::awaitable_decomposes_to;
using boost::capy::const_buffer_archetype;
using boost::capy::IoAwaitable;

// tag::write_stream_concept[]
template<typename T>
concept WriteStream =
    requires(T& stream, const_buffer_archetype buffers)
    {
        { stream.write_some(buffers) } -> IoAwaitable;
        requires awaitable_decomposes_to<
            decltype(stream.write_some(buffers)),
            std::error_code, std::size_t>;
    };
// end::write_stream_concept[]

static_assert(WriteStream<capy::any_write_stream> ==
    capy::WriteStream<capy::any_write_stream>);
static_assert(WriteStream<capy::any_write_stream>);

} // namespace concept_full

} // namespace
