//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/index.adoc.

// tag::full[]
#include <boost/capy.hpp>

namespace capy = boost::capy;

capy::task<void> echo(capy::any_stream& stream)   // <.>
{
    char buf[1024];
    for(;;)
    {
        auto [ec, n] = co_await stream.read_some(capy::make_buffer(buf));   // <.>

        auto [wec, _] = co_await capy::write(stream, capy::const_buffer(buf, n));

        if(wec)
            throw std::system_error(wec);         // <.>

        if(ec == capy::cond::eof)                 // <.>
            co_return;

        if(ec)
            throw std::system_error(ec);
    }
}

int main()
{
    // In a real application, you would obtain a stream from Corosio,
    // then start the coroutine on its io_context and run it:
    //
    //   corosio::io_context ioc;
    //   corosio::tcp_socket stream = /* from an acceptor or connect */;
    //   capy::run_async(ioc.get_executor())(echo(stream));
    //   ioc.run();
}
// end::full[]
