//
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Reference examples for boost::capy::buffer_param, injected into its documentation by
// doc/addons/extensions/reference-snippets.lua. Declared in:
//   include/boost/capy/buffers/buffer_param.hpp
//
// The tagged regions are what the reference renders; the includes,
// suppressions and namespaces around them are scaffolding. Each region gets
// its own namespace so that examples which reuse a name still compile.

#include "../doc_warnings.hpp"

#include <boost/capy.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace capy = boost::capy;

namespace {
namespace ex_1 {
// tag::example_1[]
capy::task<> write(capy::ConstBufferSequence auto buffers);   // CORRECT
capy::task<> write(capy::ConstBufferSequence auto& buffers);  // WRONG - dangling reference
// end::example_1[]
} // namespace ex_1
namespace ex_2 {
// tag::example_2[]
capy::task<> send(capy::ConstBufferSequence auto buffers)
{
    capy::buffer_param bp(buffers);
    while(true)
    {
        auto bufs = bp.data();
        if(bufs.empty())
            break;
        auto n = co_await do_something(bufs);
        bp.consume(n);
    }
}
// end::example_2[]
} // namespace ex_2
namespace ex_3 {
// tag::example_3[]
class base
{
public:
    template<capy::ConstBufferSequence BS>
    capy::task<> write(BS buffers)
    {
        capy::const_buffer_param<BS> bp(buffers);
        while(true)
        {
            auto bufs = bp.data();
            if(bufs.empty())
                break;
            std::size_t n = 0;
            co_await write_impl(bufs, n);
            bp.consume(n);
        }
    }

protected:
    virtual capy::task<> write_impl(
        std::span<capy::const_buffer> buffers,
        std::size_t& bytes_written) = 0;
};
// end::example_3[]
} // namespace ex_3

} // namespace
