//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_STREAM_HPP
#define BOOST_CAPY_CONCEPT_STREAM_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/write_stream.hpp>

namespace boost {
namespace capy {

/** Requires a type to satisfy both `ReadStream` and `WriteStream`.

    A type satisfies `Stream` if it satisfies both @ref ReadStream
    and @ref WriteStream.

    @par Syntactic Requirements
    @li Must satisfy @ref ReadStream
    @li Must satisfy @ref WriteStream

    @par Semantic Requirements
    The semantics are the union of @ref ReadStream and @ref WriteStream.
    The stream supports bidirectional I/O through `read_some` and
    `write_some` operations.

    @par Example
    @par !example example


    @see ReadStream, WriteStream
*/
template<typename T>
concept Stream = ReadStream<T> && WriteStream<T>;

} // namespace capy
} // namespace boost

#endif
