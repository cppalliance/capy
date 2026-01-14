//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/core/polystore.hpp>
#include <boost/capy/zlib/deflate.hpp>

#include "stream_cast.hpp"

#include <boost/core/detail/static_assert.hpp>

#include <zlib.h>

namespace boost {
namespace capy {
namespace zlib {

BOOST_CORE_STATIC_ASSERT(sizeof(stream) == sizeof(z_stream_s));
BOOST_CORE_STATIC_ASSERT(is_layout_identical<stream, z_stream_s>());

//------------------------------------------------

class deflate_service_impl
    : public deflate_service
{
public:
    using key_type = deflate_service;

    explicit
    deflate_service_impl(
        capy::polystore&) noexcept
    {
    }

    ~deflate_service_impl()
    {
    }

    char const*
    version() const noexcept override
    {
        return zlibVersion();
    }

    int
    init(
        stream& st,
        int level) const override
    {
        stream_cast sc(st);
        return deflateInit(sc.get(), level);
    }

    int
    init2(
        stream& st,
        int level,
        int method,
        int windowBits,
        int memLevel,
        int strategy) const override
    {
        stream_cast sc(st);
        return deflateInit2(sc.get(),
            level, method, windowBits,
            memLevel, strategy);
    }

    int
    set_dict(
        stream& st,
        unsigned char const* dict,
        unsigned len) const override
    {
        stream_cast sc(st);
        return deflateSetDictionary(sc.get(), dict, len);
    }

    int
    get_dict(
        stream& st,
        unsigned char* dest,
        unsigned* len) const override
    {
        stream_cast sc(st);
        return deflateGetDictionary(sc.get(), dest, len);
    }

    int
    dup(
        stream& dest,
        stream& src) const override
    {
        stream_cast sc0(dest);
        stream_cast sc1(src);
        return deflateCopy(sc0.get(), sc1.get());
    }

    int
    deflate(
        stream& st,
        int flush) const override
    {
        stream_cast sc(st);
        return ::deflate(sc.get(), flush);
    }

    int
    deflate_end(
        stream& st) const override
    {
        stream_cast sc(st);
        return deflateEnd(sc.get());
    }

    int
    reset(
        stream& st) const override
    {
        stream_cast sc(st);
        return deflateReset(sc.get());
    }

    int
    params(
        stream& st,
        int level,
        int strategy) const override
    {
        stream_cast sc(st);
        return deflateParams(sc.get(), level, strategy);
    }

    std::size_t
    bound(
        stream& st,
        unsigned long sourceLen) const override
    {
        stream_cast sc(st);
        return deflateBound(sc.get(), sourceLen);
    }

    int
    pending(
        stream& st,
        unsigned* pending,
        int* bits) const override
    {
        stream_cast sc(st);
        return deflatePending(sc.get(), pending, bits);
    }

    int
    prime(
        stream& st,
        int bits,
        int value) const override
    {
        stream_cast sc(st);
        return deflatePrime(sc.get(), bits, value);
    }

    int
    set_header(
        stream& st,
        void* header) const override
    {
        stream_cast sc(st);
        return deflateSetHeader(sc.get(),
            reinterpret_cast<gz_header*>(header));
    }
};

deflate_service&
install_deflate_service(polystore& ctx)
{
    return ctx.emplace<deflate_service_impl>(ctx);
}

} // zlib
} // capy
} // boost
