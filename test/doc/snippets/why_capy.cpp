//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//
// Fragments deliberately leave named results unused; page comments
// explain the values instead.

// Fragments deliberately leave results and bindings unused; the pages
// explain the values in prose instead.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-function"
// gcc 15 with sanitizers misattributes coroutine frame delete paths
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

// tag::invariant[]
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/task.hpp>

using namespace boost::capy;

// Every resumption happens on the executor this task was started with, so
// `count` needs no mutex: nothing else can run between the suspension
// points.
task<> count_messages(any_stream& stream, int& count)
{
    char buf[64];
    for(;;)
    {
        auto [ec, n] = co_await stream.read_some(make_buffer(buf));
        if(ec)
            break;
        ++count;
    }
}
// end::invariant[]
