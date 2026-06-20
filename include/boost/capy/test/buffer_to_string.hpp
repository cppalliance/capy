//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_TEST_BUFFER_TO_STRING_HPP
#define BOOST_CAPY_TEST_BUFFER_TO_STRING_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>

#include <string>

namespace boost {
namespace capy {
namespace test {

/** Convert one or more buffer sequences to a string.

    This function concatenates the bytes from all provided buffer
    sequences into a single string. With a single argument, it
    converts that buffer sequence to a string. With multiple
    arguments, it concatenates them in order.

    @par Example
    @code
    // Single buffer sequence
    const_buffer cb( "hello", 5 );
    std::string s = buffer_to_string( cb );  // "hello"

    // Multiple buffer sequences (concatenation)
    const_buffer b1( "hello", 5 );
    const_buffer b2( " world", 6 );
    std::string s = buffer_to_string( b1, b2 );  // "hello world"

    // With bufgrind splits
    bufgrind bg( cb );
    while( bg ) {
        auto [b1, b2] = co_await bg.next();
        BOOST_TEST_EQ( buffer_to_string( b1.data(), b2.data() ), "hello" );
    }
    @endcode

    @param bufs One or more buffer sequences to concatenate.

    @return A string containing all bytes from the buffer sequences.
*/
template<ConstBufferSequence... Buffers>
std::string
buffer_to_string(Buffers const&... bufs)
{
    std::string result;
    result.reserve((buffer_size(bufs) + ...));
    auto append = [&](auto const& bs) {
        auto const e = end(bs);
        for(auto it = begin(bs); it != e; ++it)
        {
            const_buffer b(*it);
            result.append(
                static_cast<char const*>(b.data()),
                b.size());
        }
    };
    (append(bufs), ...);
    return result;
}

} // test
} // capy
} // boost

#endif
