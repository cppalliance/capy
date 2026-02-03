//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "echo.hpp"
#include <boost/capy.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/test/fuse.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <iostream>

using namespace boost::capy;

void test_with_mock()
{
    test::fuse f;
    test::stream mock(f);
    mock.provide("Hello, ");
    mock.provide("World!\n");
    // Stream returns eof when no more data is available
    
    // Using pointer construction (&mock) for reference semantics - the
    // wrapper does not take ownership, so mock must outlive stream.
    any_stream stream{&mock};  // any_stream
    test::run_blocking()(myapp::echo_session(stream));
    
    std::cout << "Echo output: " << mock.data() << "\n";
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
