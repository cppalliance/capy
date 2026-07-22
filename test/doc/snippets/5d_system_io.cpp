//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/5.buffers/5d.system-io.adoc. The
// OS-level fragments compile only where the platform headers exist;
// guards keep this TU portable while the tags stay extractable.

// tag::include_buffers[]
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
// end::include_buffers[]

#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/write.hpp>

#include <array>
#include <cstddef>

#if __has_include(<sys/uio.h>)
#include <sys/uio.h>
#include <unistd.h>
#define BOOST_CAPY_DOC_HAS_POSIX_IO
#endif

#if __has_include(<liburing.h>)
#include <liburing.h>
#endif

namespace {

using namespace boost::capy;

// tag::write_some_signature[]
template<ConstBufferSequence Buffers>
io_task<std::size_t> write_some(Buffers buffers);
// end::write_some_signature[]

#if defined(BOOST_CAPY_DOC_HAS_POSIX_IO)

// The page fragments deliberately ignore syscall results; the point
// they make is the syscall count, not error handling.

[[maybe_unused]] void
write_without_gather(
    int fd, char const* header, std::size_t header_len,
    char const* body, std::size_t body_len)
{
    // tag::two_syscalls[]
    write(fd, header, header_len);  // syscall 1
    write(fd, body, body_len);      // syscall 2
    // end::two_syscalls[]
}

[[maybe_unused]] void
write_with_gather(
    int fd, char* header, std::size_t header_len,
    char* body, std::size_t body_len)
{
    // tag::gather_syscall[]
    iovec iov[2] = {{header, header_len}, {body, body_len}};
    writev(fd, iov, 2);  // single syscall
    // end::gather_syscall[]
}

// tag::cached_iovecs[]
// Build once, use many times
struct message_buffers
{
    std::array<iovec, 3> iovecs;

    void set_header(void const* p, std::size_t n);
    void set_body(void const* p, std::size_t n);
    void set_footer(void const* p, std::size_t n);
};
// end::cached_iovecs[]

#endif // BOOST_CAPY_DOC_HAS_POSIX_IO

#if __has_include(<liburing.h>)

// Never instantiated: liburing declarations are compile-checked
// without requiring -luring at link time.
template<class = void>
void
use_registered_buffers(
    io_uring* ring, iovec const* buffers, unsigned count,
    io_uring_sqe* sqe, int fd, void const* buf, unsigned len,
    unsigned long long offset, int buf_index)
{
    // tag::io_uring_fixed[]
    // Registration (done once)
    io_uring_register_buffers(ring, buffers, count);

    // Use (fast path - no translation)
    io_uring_prep_write_fixed(sqe, fd, buf, len, offset, buf_index);
    // end::io_uring_fixed[]
}

#endif // __has_include(<liburing.h>)

const_buffer
assemble_message()
{
    static char const msg[] = "assembled message";
    return const_buffer(msg, sizeof(msg) - 1);
}

[[maybe_unused]] task<>
minimize_buffer_count(test::stream& stream)
{
    // tag::minimize_buffer_count[]
    // Prefer: single buffer when possible
    auto buf = assemble_message();  // Build in one buffer
    co_await write(stream, buf);

    // Avoid: many tiny buffers
    std::array<const_buffer, 100> tiny_bufs;
    co_await write(stream, tiny_bufs);  // 100-element translation
    // end::minimize_buffer_count[]
}

} // namespace
