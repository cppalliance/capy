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
#include <boost/capy/zlib/inflate.hpp>

#include "stream_cast.hpp"

#include <boost/core/detail/static_assert.hpp>

#include <zlib.h>

namespace boost {
namespace capy {
namespace zlib {

BOOST_CORE_STATIC_ASSERT(sizeof(stream) == sizeof(z_stream_s));
BOOST_CORE_STATIC_ASSERT(is_layout_identical<stream, z_stream_s>());

//------------------------------------------------

class inflate_service_impl
    : public inflate_service
{
public:
    using key_type = inflate_service;

    explicit
    inflate_service_impl(
        capy::polystore&) noexcept
    {
    }

    ~inflate_service_impl()
    {
    }

    char const*
    version() const noexcept override
    {
        return zlibVersion();
    }

    int
    init(
        stream& st) const override
    {
        stream_cast sc(st);
        return inflateInit(sc.get());
    }

    int
    init2(
        stream& st,
        int windowBits) const override
    {
        stream_cast sc(st);
        return inflateInit2(sc.get(), windowBits);
    }

    int
    inflate(
        stream& st,
        int flush) const override
    {
        stream_cast sc(st);
        return ::inflate(sc.get(), flush);
    }

    int
    inflate_end(
        stream& st) const override
    {
        stream_cast sc(st);
        return inflateEnd(sc.get());
    }

    int
    set_dict(
        stream& st,
        unsigned char const* dict,
        unsigned len) const override
    {
        stream_cast sc(st);
        return inflateSetDictionary(sc.get(), dict, len);
    }

    int
    get_dict(
        stream& st,
        unsigned char* dest,
        unsigned* len) const override
    {
        stream_cast sc(st);
        return inflateGetDictionary(sc.get(), dest, len);
    }

    int
    sync(
        stream& st) const override
    {
        stream_cast sc(st);
        return inflateSync(sc.get());
    }

    int
    dup(
        stream& dest,
        stream& src) const override
    {
        stream_cast sc0(dest);
        stream_cast sc1(src);
        return inflateCopy(sc0.get(), sc1.get());
    }

    int
    reset(
        stream& st) const override
    {
        stream_cast sc(st);
        return inflateReset(sc.get());
    }

    int
    reset2(
        stream& st,
        int windowBits) const override
    {
        stream_cast sc(st);
        return inflateReset2(sc.get(), windowBits);
    }

    int
    prime(
        stream& st,
        int bits,
        int value) const override
    {
        stream_cast sc(st);
        return inflatePrime(sc.get(), bits, value);
    }

    long
    mark(
        stream& st) const override
    {
        stream_cast sc(st);
        return inflateMark(sc.get());
    }

    int
    get_header(
        stream& st,
        void* header) const override
    {
        stream_cast sc(st);
        return inflateGetHeader(sc.get(),
            reinterpret_cast<gz_headerp>(header));
    }

    int
    back_init(
        stream& st,
        int windowBits,
        unsigned char* window) const override
    {
        stream_cast sc(st);
        return inflateBackInit(sc.get(), windowBits, window);
    }

    int
    back_end(
        stream& st) const override
    {
        stream_cast sc(st);
        return inflateBackEnd(sc.get());
    }

    unsigned long
    compile_flags() const override
    {
        return zlibCompileFlags();
    }
};

inflate_service&
install_inflate_service(polystore& ctx)
{
    return ctx.emplace<inflate_service_impl>(ctx);
}

} // zlib
} // capy
} // boost
