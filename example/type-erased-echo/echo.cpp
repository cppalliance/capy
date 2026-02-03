//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "echo.hpp"
#include <boost/capy/read.hpp>
#include <boost/capy/write.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/buffers/make_buffer.hpp>

namespace myapp {

using namespace boost::capy;

task<> echo_session(any_stream& stream)
{
    char buffer[1024];
    
    for (;;)
    {
        // Read some data
        // ec: std::error_code, n: std::size_t
        auto [ec, n] = co_await stream.read_some(make_buffer(buffer));
        
        if (ec == cond::eof)
            co_return;  // Client closed connection
        
        if (ec)
            throw std::system_error(ec);
        
        // Echo it back
        // wec: std::error_code, wn: std::size_t
        auto [wec, wn] = co_await write(stream, const_buffer(buffer, n));
        
        if (wec)
            throw std::system_error(wec);
    }
}

} // namespace myapp
