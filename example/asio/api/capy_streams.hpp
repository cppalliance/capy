//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef CAPY_EXAMPLE_CAPY_STREAMS_HPP
#define CAPY_EXAMPLE_CAPY_STREAMS_HPP

// tag::header[]
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/io/any_stream.hpp>

#include <utility>

namespace boost { namespace asio { class io_context; } }

namespace net = boost::asio;
namespace capy = boost::capy;

// A type-erased asio::io_context
class asio_context : public capy::execution_context
{
    struct impl;
    impl* impl_;

public:
    using executor_type = capy::executor_ref;

    asio_context();
    ~asio_context();

    asio_context(asio_context const&) = delete;
    asio_context& operator=(asio_context const&) = delete;

    net::io_context& context() noexcept;
    executor_type get_executor() noexcept;

    void run();
};

// Create a connected pair of any_stream
std::pair<capy::any_stream, capy::any_stream>
make_stream_pair(asio_context& ctx);
// end::header[]

#endif
