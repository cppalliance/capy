//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "../doc_warnings.hpp"

// tag::invariant[]
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/task.hpp>

namespace capy = boost::capy;

// Every resumption happens on the executor this task was started with, so
// `count` needs no mutex: nothing else can run between the suspension
// points.
capy::task<> count_messages(capy::any_stream& stream, int& count)
{
    char buf[64];
    for(;;)
    {
        auto [ec, n] = co_await stream.read_some(capy::make_buffer(buf));
        if(ec)
            break;
        ++count;
    }
}
// end::invariant[]
