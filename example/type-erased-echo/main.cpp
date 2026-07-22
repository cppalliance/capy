//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// tag::full[]
#include "echo.hpp"
#include <boost/capy.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <iostream>

namespace capy = boost::capy;

void test_with_mock()
{
    auto [a, b] = capy::test::make_stream_pair();
    b.provide("Hello, ");
    b.provide("World!\n");
    b.close();
    
    // Using pointer construction (&a) for reference semantics - the
    // wrapper does not take ownership, so a must outlive stream.
    capy::any_stream stream{&a};  // any_stream
    capy::test::run_blocking()(myapp::echo_session(stream));
    
    std::cout << "Echo output: " << b.data() << "\n";
}

// With real sockets (using Corosio), you would write:
//
// task<> handle_client(corosio::tcp::socket socket)
// {
//     // Value construction moves socket into wrapper (transfers ownership)
//     any_stream stream{std::move(socket)};
//     co_await myapp::echo_session(stream);
// }

int main()
{
    test_with_mock();
    return 0;
}
// end::full[]
