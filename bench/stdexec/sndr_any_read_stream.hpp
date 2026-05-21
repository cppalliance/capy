//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BENCH_STDEXEC_SNDR_ANY_READ_STREAM_HPP
#define BOOST_CAPY_BENCH_STDEXEC_SNDR_ANY_READ_STREAM_HPP

#include <boost/capy/buffers.hpp>

#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

#include <cstddef>
#include <memory>
#include <system_error>
#include <utility>

/// Type-erased sender returned from any_read_stream::read_some.
using any_read_sender =
    exec::any_sender<exec::any_receiver<stdexec::completion_signatures<
        stdexec::set_value_t(std::size_t),
        stdexec::set_error_t(std::error_code),
        stdexec::set_error_t(std::exception_ptr),
        stdexec::set_stopped_t()>>>;

/// Value-type-erased read stream.
///
/// Holds a concrete stream by value via a polymorphic
/// model. read_some returns any_read_sender so the
/// concrete stream's sender type is hidden at the
/// stream's API boundary.
class sndr_any_read_stream
{
    struct concept_t
    {
        virtual any_read_sender read_some(
            boost::capy::mutable_buffer) = 0;
        virtual ~concept_t() = default;
    };

    template <class Stream>
    struct model_t : concept_t
    {
        Stream stream_;

        explicit model_t(Stream s) : stream_(std::move(s)) {}

        any_read_sender read_some(
            boost::capy::mutable_buffer buf) override
        {
            return any_read_sender(stream_.read_some(buf));
        }
    };

    std::unique_ptr<concept_t> impl_;

public:
    template <class Stream>
    explicit sndr_any_read_stream(Stream s)
        : impl_(new model_t<Stream>(std::move(s)))
    {}

    any_read_sender read_some(boost::capy::mutable_buffer buf)
    {
        return impl_->read_some(buf);
    }
};

#endif
