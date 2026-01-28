//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/buffers/circular_dynamic_buffer.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/flat_dynamic_buffer.hpp>
#include <boost/asio/buffer.hpp>
#include <span>

#include <type_traits>

#include "test_buffers.hpp"

namespace boost {
namespace capy {

// These tests check to make sure the Asio buffer
// sequences and our buffer sequences are interoperable

// our buffer is convertible to asio buffer
static_assert(  std::is_convertible<const_buffer,   asio::const_buffer>::value);
static_assert(! std::is_convertible<const_buffer,   asio::mutable_buffer>::value);
static_assert(  std::is_convertible<mutable_buffer, asio::const_buffer>::value);
static_assert(  std::is_convertible<mutable_buffer, asio::mutable_buffer>::value);

// asio buffer is convertible to our buffer
static_assert(  std::is_convertible<asio::const_buffer,   const_buffer>::value);
static_assert(! std::is_convertible<asio::const_buffer,   mutable_buffer>::value);
static_assert(  std::is_convertible<asio::mutable_buffer, const_buffer>::value);
static_assert(  std::is_convertible<asio::mutable_buffer, mutable_buffer>::value);

// span of asio buffer is an asio sequence
static_assert(  asio::is_const_buffer_sequence<  std::span<asio::const_buffer>>::value);
static_assert(  asio::is_const_buffer_sequence<  std::span<asio::mutable_buffer>>::value);
static_assert(! asio::is_mutable_buffer_sequence<std::span<asio::const_buffer>>::value);
static_assert(  asio::is_mutable_buffer_sequence<std::span<asio::mutable_buffer>>::value);

// span of our buffer is an asio sequence
static_assert(  asio::is_const_buffer_sequence<std::span<const_buffer>>::value);
static_assert(  asio::is_const_buffer_sequence<std::span<mutable_buffer>>::value);
static_assert(! asio::is_mutable_buffer_sequence<std::span<const_buffer>>::value);
static_assert(  asio::is_mutable_buffer_sequence<std::span<mutable_buffer>>::value);

// span of asio buffer is our sequence
static_assert(  ConstBufferSequence<  std::span<asio::const_buffer const>>);
static_assert(  ConstBufferSequence<  std::span<asio::mutable_buffer const>>);
static_assert(! MutableBufferSequence<std::span<asio::const_buffer const>>);
static_assert(  MutableBufferSequence<std::span<asio::mutable_buffer const>>);

// span of our buffer is our sequence
static_assert(  ConstBufferSequence<  std::span<const_buffer const>>);
static_assert(  ConstBufferSequence<  std::span<mutable_buffer const>>);
static_assert(! MutableBufferSequence<std::span<const_buffer const>>);
static_assert(  MutableBufferSequence<std::span<mutable_buffer const>>);

// satisfy asio metafunctions
static_assert(  asio::is_const_buffer_sequence<   const_buffer>::value);
static_assert(  asio::is_const_buffer_sequence<   const_buffer_pair>::value);
static_assert(  asio::is_const_buffer_sequence<   circular_dynamic_buffer::const_buffers_type>::value);
static_assert(  asio::is_const_buffer_sequence<   flat_dynamic_buffer::const_buffers_type>::value);
static_assert(  asio::is_const_buffer_sequence<   mutable_buffer>::value);
static_assert(  asio::is_const_buffer_sequence<   mutable_buffer_pair>::value);
static_assert(  asio::is_const_buffer_sequence<   circular_dynamic_buffer::mutable_buffers_type>::value);
static_assert(  asio::is_const_buffer_sequence<   flat_dynamic_buffer::mutable_buffers_type>::value);
static_assert(! asio::is_mutable_buffer_sequence< const_buffer>::value);
static_assert(! asio::is_mutable_buffer_sequence< const_buffer_pair>::value);
static_assert(! asio::is_mutable_buffer_sequence< circular_dynamic_buffer::const_buffers_type>::value);
static_assert(! asio::is_mutable_buffer_sequence< flat_dynamic_buffer::const_buffers_type>::value);
static_assert(  asio::is_mutable_buffer_sequence< mutable_buffer>::value);
static_assert(  asio::is_mutable_buffer_sequence< mutable_buffer_pair>::value);
static_assert(  asio::is_mutable_buffer_sequence< circular_dynamic_buffer::mutable_buffers_type>::value);
static_assert(  asio::is_mutable_buffer_sequence< flat_dynamic_buffer::mutable_buffers_type>::value);

struct asio_test
{
    void
    run()
    {
        // these should compile
        mutable_buffer mb;
        const_buffer cb;
        asio::mutable_buffer amb;
        asio::const_buffer acb;

        asio::buffer_copy( mb,  cb);
        asio::buffer_copy( mb,  mb);
        asio::buffer_copy(amb,  cb);
        asio::buffer_copy(amb,  mb);
        asio::buffer_copy( mb, acb);
        asio::buffer_copy( mb, amb);
    }
};

TEST_SUITE(
    asio_test,
    "boost.capy.buffers.asio");

} // capy
} // boost
