//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "random.hpp"
#include <boost/capy/detail/except.hpp>
#include <boost/system/error_code.hpp>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <bcrypt.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "bcrypt.lib")
#  endif
#elif defined(__linux__)
#  include <sys/random.h>
#elif defined(__APPLE__)
#  include <Security/SecRandom.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace boost {
namespace capy {
namespace bcrypt {
namespace detail {

#if defined(_WIN32)

namespace {

class rng_provider
{
    BCRYPT_ALG_HANDLE h_ = nullptr;

public:
    rng_provider()
    {
        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &h_,
            BCRYPT_RNG_ALGORITHM,
            nullptr,
            0);
        if (!BCRYPT_SUCCESS(status))
            h_ = nullptr;
    }

    ~rng_provider()
    {
        if (h_)
            BCryptCloseAlgorithmProvider(h_, 0);
    }

    rng_provider(rng_provider const&) = delete;
    rng_provider& operator=(rng_provider const&) = delete;

    bool generate(void* buf, std::size_t n) const
    {
        if (!h_)
            return false;
        NTSTATUS status = BCryptGenRandom(
            h_,
            static_cast<PUCHAR>(buf),
            static_cast<ULONG>(n),
            0);
        return BCRYPT_SUCCESS(status);
    }
};

rng_provider& get_rng()
{
    static rng_provider rng;
    return rng;
}

} // namespace

void
fill_random(void* buf, std::size_t n)
{
    if (!get_rng().generate(buf, n))
    {
        capy::detail::throw_system_error(
            system::error_code(
                static_cast<int>(GetLastError()),
                system::system_category()));
    }
}

#elif defined(__linux__)

void
fill_random(void* buf, std::size_t n)
{
    auto* p = static_cast<unsigned char*>(buf);
    while (n > 0)
    {
        ssize_t r = getrandom(p, n, 0);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            capy::detail::throw_system_error(
                system::error_code(
                    errno,
                    system::system_category()));
        }
        p += r;
        n -= static_cast<std::size_t>(r);
    }
}

#elif defined(__APPLE__)

void
fill_random(void* buf, std::size_t n)
{
    int err = SecRandomCopyBytes(kSecRandomDefault, n, buf);
    if (err != errSecSuccess)
    {
        capy::detail::throw_system_error(
            system::error_code(
                err,
                system::system_category()));
    }
}

#else

// Fallback: /dev/urandom
void
fill_random(void* buf, std::size_t n)
{
    static int fd = -1;
    if (fd < 0)
    {
        fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (fd < 0)
        {
            capy::detail::throw_system_error(
                system::error_code(
                    errno,
                    system::system_category()));
        }
    }

    auto* p = static_cast<unsigned char*>(buf);
    while (n > 0)
    {
        ssize_t r = read(fd, p, n);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            capy::detail::throw_system_error(
                system::error_code(
                    errno,
                    system::system_category()));
        }
        if (r == 0)
        {
            capy::detail::throw_runtime_error(
                "unexpected EOF from /dev/urandom");
        }
        p += r;
        n -= static_cast<std::size_t>(r);
    }
}

#endif

} // detail
} // bcrypt
} // capy
} // boost

