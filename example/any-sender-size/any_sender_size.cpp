//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Measures the operation state produced by connecting a small sender
// pipeline through exec::any_sender / exec::any_receiver, against the
// concrete (non-erased) operation state for the same pipeline. Reports
// sizeof of each and the heap allocations made by connect and start,
// counted through the replaceable operator new.

#include <exec/any_sender_of.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace ex = stdexec;

static std::size_t g_alloc_count = 0;
static std::size_t g_alloc_bytes = 0;

void* operator new(std::size_t n)
{
    ++g_alloc_count;
    g_alloc_bytes += n;
    if (void* p = std::malloc(n))
        return p;
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

struct receiver
{
    using receiver_concept = ex::receiver_t;
    std::atomic<bool>* done;
    void finish() noexcept { done->store(true); done->notify_one(); }
    void set_value(int) noexcept { finish(); }
    void set_error(std::exception_ptr) noexcept { finish(); }
    void set_stopped() noexcept { finish(); }
    ex::env<> get_env() const noexcept { return {}; }
};

using any_sender = exec::any_sender<
    exec::any_receiver<ex::completion_signatures<
        ex::set_value_t(int),
        ex::set_error_t(std::exception_ptr),
        ex::set_stopped_t()>>>;

template<class Sender>
void measure(char const* label, Sender sender)
{
    using concrete_op = ex::connect_result_t<Sender, receiver>;
    using erased_op = ex::connect_result_t<any_sender, receiver>;

    std::atomic<bool> done{false};
    any_sender erased{sender};
    g_alloc_count = 0;
    g_alloc_bytes = 0;
    auto op = ex::connect(std::move(erased), receiver{&done});
    auto const connect_count = g_alloc_count;
    auto const connect_bytes = g_alloc_bytes;
    g_alloc_count = 0;
    ex::start(op);
    auto const start_count = g_alloc_count;
    done.wait(false);

    done = false;
    auto cop = ex::connect(std::move(sender), receiver{&done});
    g_alloc_count = 0;
    ex::start(cop);
    done.wait(false);

    std::printf("%s\n", label);
    std::printf("  erased op state:   %zu bytes, connect allocates %zu "
        "(%zu bytes), start allocates %zu\n", sizeof(erased_op),
        connect_count, connect_bytes, start_count);
    std::printf("  concrete op state: %zu bytes, connect allocates 0, "
        "start allocates %zu\n", sizeof(concrete_op), g_alloc_count);
}

int main()
{
    std::printf("stdexec any_sender small buffer: 64 bytes\n");
    measure("starts_on(inline_scheduler, just(42))",
        ex::starts_on(ex::inline_scheduler{}, ex::just(42)));

    exec::static_thread_pool pool(1);
    measure("starts_on(static_thread_pool::scheduler, just(42))",
        ex::starts_on(pool.get_scheduler(), ex::just(42)));
    pool.request_stop();
}
