//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_SNDR_ANY_READ_STREAM_HPP
#define BOOST_CAPY_BENCH_SNDR_ANY_READ_STREAM_HPP

#include "sndr_any_read_sender.hpp"

#include <boost/capy/buffers.hpp>

#include <utility>

/// Standalone value-type erased sender stream.
///
/// Mirrors capy::any_read_stream: stores any sender stream behind
/// a vtable, heap-allocated. Does NOT inherit from
/// sndr_io_read_stream — this is a fully independent erasure
/// mechanism.
class sndr_any_read_stream
{
    using read_some_fn = sndr_any_read_sender(*)(
        void*, boost::capy::mutable_buffer);
    using destroy_fn = void(*)(void*) noexcept;

    void* stream_;
    read_some_fn read_some_;
    destroy_fn destroy_;

public:
    template <class Stream>
    explicit sndr_any_read_stream(Stream s)
    {
        stream_ = new Stream(std::move(s));

        read_some_ = +[](void* stor,
            boost::capy::mutable_buffer buf)
            -> sndr_any_read_sender
        {
            auto& stream = *static_cast<Stream*>(stor);
            return sndr_any_read_sender{stream.read_some(buf)};
        };

        destroy_ = +[](void* stor) noexcept {
            delete static_cast<Stream*>(stor);
        };
    }

    ~sndr_any_read_stream() { destroy_(stream_); }

    sndr_any_read_stream(sndr_any_read_stream const&) = delete;
    sndr_any_read_stream& operator=(sndr_any_read_stream const&) = delete;
    sndr_any_read_stream(sndr_any_read_stream&&) = delete;
    sndr_any_read_stream& operator=(sndr_any_read_stream&&) = delete;

    sndr_any_read_sender
        read_some(boost::capy::mutable_buffer buf)
    {
        return read_some_(stream_, buf);
    }
};

#endif
