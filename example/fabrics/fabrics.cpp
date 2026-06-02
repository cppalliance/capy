//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// The transport-neutral, non-GPU listings from P4251R0: the byte-oriented
// compound-result pattern (capy only) and the HPC-fabric send-call
// signatures (libibverbs / libfabric / UCX). Nothing is executed; the
// build itself is the check. Each fabric block builds only when found.

#include <boost/capy.hpp>
#include <boost/capy/test/stream.hpp>

#include <cstddef>
#include <system_error>

namespace capy = boost::capy;

namespace {

// A byte-oriented read delivers (error_code, n) via structured bindings;
// the coroutine branches on a partial-read condition (the peer reset after
// n bytes arrived) with no sender channel to choose. The same compound
// result is what RDMA work completions carry.
[[maybe_unused]] capy::task<>
read_with_reset(capy::test::stream& s)
{
    std::byte buf[64];
    auto [ec, n] = co_await s.read_some(
        capy::mutable_buffer(buf, sizeof buf));
    if(ec == std::errc::connection_reset)
    {
        // 'n' bytes arrived before the reset.
        (void) n;
        co_return;
    }
    (void) n;
}

} // namespace

#if defined(CAPY_EXAMPLE_HAS_IBVERBS)
#include <infiniband/verbs.h>

namespace {

// libibverbs: completion via a completion-channel file descriptor.
[[maybe_unused]] void
sig_ibverbs()
{
    ibv_qp* qp = nullptr;
    ibv_send_wr wr{};
    ibv_send_wr* bad_wr = nullptr;
    (void) ibv_post_send(qp, &wr, &bad_wr);
}

} // namespace
#endif

#if defined(CAPY_EXAMPLE_HAS_LIBFABRIC)
#include <rdma/fi_endpoint.h>

namespace {

// libfabric: completion via a completion-queue poll.
[[maybe_unused]] void
sig_libfabric()
{
    fid_ep* ep = nullptr;
    char buffer[16];
    fi_addr_t dest_addr = 0;
    void* context = nullptr;
    (void) fi_send(ep, buffer, sizeof buffer, nullptr, dest_addr, context);
}

} // namespace
#endif

#if defined(CAPY_EXAMPLE_HAS_UCX)
#include <ucp/api/ucp.h>

namespace {

// UCX: completion via a callback from the progress engine.
[[maybe_unused]] void
sig_ucx()
{
    ucp_ep_h ep = nullptr;
    char buffer[16];
    ucp_tag_t tag = 0;
    ucp_request_param_t param{};
    (void) ucp_tag_send_nbx(ep, buffer, sizeof buffer, tag, &param);
}

} // namespace
#endif

// The target exists to prove the listings are type-correct; it is not run.
int main()
{
    return 0;
}
