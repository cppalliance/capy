//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "fault.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <pthread.h>

/* The replacement libc functions ("shadows") are defined in the test
   executable. libstdc++ calls pthread_create through the dynamic loader,
   which finds the executable's definition first. Each shadow asks
   should_fail() and otherwise forwards to the real function, which it
   finds with dlsym(RTLD_NEXT).

   A sanitizer runtime linked statically into the executable defines
   its own pthread_create, and the shadow takes its place. Forwarding
   to libc would then create threads the sanitizer never sees, so the
   shadow forwards to the runtime's __interceptor_pthread_create, which
   the runtime exports for this purpose. The reference is weak and is
   null without a sanitizer.

   The readback at startup checks that the loader really binds
   pthread_create to the shadow. Without it, a build that stripped the
   shadow from the dynamic symbol table would leave every fault silently
   dead.
*/

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define BOOST_CAPY_FAULT_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define BOOST_CAPY_FAULT_SANITIZER 1
#endif
#endif
#if !defined(BOOST_CAPY_FAULT_SANITIZER)
#define BOOST_CAPY_FAULT_SANITIZER 0
#endif

#if BOOST_CAPY_FAULT_SANITIZER
extern "C" void __interceptor_pthread_create() __attribute__((weak));
#endif

namespace boost {
namespace capy {
namespace test {
namespace fault {

namespace {

// The arm for this thread. Plain aggregate so the shadows touch
// nothing that could allocate or lock.
struct slot
{
    sys which = sys::pthread_create;
    int err = 0;
    unsigned nth = 1;
    unsigned seen = 0;
    bool armed = false;
    bool fired = false;
    // Tracks the owning fault_scope's lifetime, independent of `armed`:
    // should_fail clears `armed` when the fault fires, but the scope
    // keeps the arm until it is destroyed.
    bool owned = false;
};

thread_local slot tls_arm;

[[noreturn]] void
die(char const* msg) noexcept
{
    std::fputs(msg, stderr);
    std::fputc('\n', stderr);
    std::abort();
}

// Resolve the real entry point behind a shadow, or die naming it
// rather than crash inside the shadow.
void*
real_symbol(char const* name) noexcept
{
#if BOOST_CAPY_FAULT_SANITIZER
    void (*const interceptor)() = &__interceptor_pthread_create;
    if(interceptor && std::strcmp(name, "pthread_create") == 0)
        return reinterpret_cast<void*>(interceptor);
#endif
    void* p = ::dlsym(RTLD_NEXT, name);
    if(!p)
    {
        char msg[160];
        std::snprintf(
            msg, sizeof(msg),
            "fault harness: %s has no implementation behind the shadow",
            name);
        die(msg);
    }
    return p;
}

// Return true when the current call must fail; the error is already
// in errno.
bool
should_fail(sys which) noexcept
{
    slot& s = tls_arm;
    if(!s.armed || s.which != which)
        return false;
    if(++s.seen != s.nth)
        return false;
    s.armed = false;
    s.fired = true;
    errno = s.err;
    return true;
}

// Whether the loader binds pthread_create to the shadow. A null
// binding means there is no dynamic symbol to interpose, as in a
// static executable: the shadow is not live, and tests skip. Any
// other binding is a broken interposition and aborts.
bool const pthread_create_live = []
{
    void* bound = ::dlsym(RTLD_DEFAULT, "pthread_create");
    void* hook = reinterpret_cast<void*>(&::pthread_create);
    if(bound == hook)
        return true;
    if(!bound)
        return false;
    std::fprintf(
        stderr, "fault harness: pthread_create is bound to %p, hook is %p\n",
        bound, hook);
    die("fault harness: the pthread_create shadow is not interposing");
}();

} // namespace

fault_scope::fault_scope(sys which, int err, unsigned nth)
{
    if(tls_arm.owned)
        die("fault_scope: a fault is already armed on this thread");
    tls_arm = slot{};
    tls_arm.which = which;
    tls_arm.err = err;
    tls_arm.nth = nth;
    tls_arm.armed = true;
    tls_arm.owned = true;
}

fault_scope::~fault_scope()
{
    tls_arm.armed = false;
    tls_arm.owned = false;
}

bool
fault_scope::fired() const noexcept
{
    return tls_arm.fired;
}

unsigned
fault_scope::count() const noexcept
{
    return tls_arm.seen;
}

bool
hook_is_live(sys which) noexcept
{
    switch(which)
    {
    case sys::pthread_create:
        return pthread_create_live;
    }
    return false;
}

} // fault
} // test
} // capy
} // boost

// pthread_create reports through its return value, not errno. The
// armed error is returned directly, and std::thread's constructor
// turns it into a std::system_error.
extern "C" int
pthread_create(
    pthread_t* t,
    pthread_attr_t const* a,
    void* (*fn)(void*),
    void* arg) noexcept
{
    using namespace boost::capy::test::fault;
    using real_t = int (*)(
        pthread_t*, pthread_attr_t const*, void* (*)(void*), void*);
    static auto const real =
        reinterpret_cast<real_t>(real_symbol("pthread_create"));
    if(should_fail(sys::pthread_create))
        return errno ? errno : EAGAIN;
    return real(t, a, fn, arg);
}
