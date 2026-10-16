//
// Copyright (c) 2026 Andrzej Krzemieński (akrzemi1@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/6.streams/6a.concepts.adoc. Pages
// include the tagged regions; scaffolding stays outside the tags.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/stream.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/results.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/run_blocking.hpp>
#include <boost/capy/test/stream.hpp>
#include <boost/capy/write.hpp>

#include <cstddef>
#include <string_view>
#include <system_error>

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {

// tag::read_some_demo[]
capy::task<> test_read_some(capy::ReadStream auto& stream, capy::MutableBufferSequence auto buffer)
{
  auto [ec, n] = co_await stream.read_some(buffer);

  // decltype(ec) == std::error_code
  // decltype(n) == std::size_t
} 
// end::read_some_demo[]

// tag::read_some_pattern[]
capy::task<> keep_reading(capy::ReadStream auto& stream, capy::MutableBufferSequence auto buffer)
{
  for (;;) 
  {
    auto [ec, n] = co_await stream.read_some(buffer);

    // process `n` bytes from `buffer` (even if `bool(ec) == true`)
    
    if (ec)
      break;
  } 
} 
// end::read_some_pattern[]

capy::task<> demonstrate_conditions(capy::ReadStream auto& stream, capy::MutableBufferSequence auto buffer)
{
  // tag::conditions[]
  auto [ec, n] = co_await stream.read_some(buffer);
  if (ec == capy::cond::eof) // <1>
    co_return;
  // end::conditions[]
} 


// tag::write_some_demo[]
capy::task<> test_write_some(capy::WriteStream auto& stream, capy::ConstBufferSequence auto buffer)
{
  auto [ec, n] = co_await stream.write_some(buffer);

  // decltype(ec) == std::error_code
  // decltype(n) == std::size_t
} 
// end::write_some_demo[]

capy::task<> intro(capy::ReadStream auto& stream, capy::ConstBufferSequence auto buf)
{
// tag::write_loop[]
for (;;)  // <1>
{
  auto [ec, n] = co_await stream.read_some(capy::make_buffer(buf));         // <2>
  auto [wec, _] = co_await capy::write(stream, capy::const_buffer(buf, n)); // <3>

  // ...
}
// end::write_loop[]
}

// tag::custom_read_stream[]
// A type models ReadStream by offering read_some. There is no base class
// and no registration step: the concept inspects the member and the type
// its awaitable produces.
class string_source
{
    std::string_view rest_;

public:
    explicit
    string_source(std::string_view s) noexcept
        : rest_(s)
    {
    }

    template<capy::MutableBufferSequence MB>
    capy::task<capy::results<std::size_t>>
    read_some(MB buffers)
    {
        using result = capy::results<std::size_t>;

        if(rest_.empty())
            co_return result{capy::error::eof, 0};

        std::size_t const n = capy::buffer_copy(
            buffers, capy::const_buffer(rest_.data(), rest_.size()));
        rest_.remove_prefix(n);
        co_return result{std::error_code(), n};
    }
};
// end::custom_read_stream[]

// tag::concept_checks[]
// Conformance is a compile-time question, so ask it at compile time.
static_assert(capy::ReadStream<string_source>);

// A read-only type is not a WriteStream, and so not a Stream either.
static_assert(! capy::WriteStream<string_source>);
static_assert(! capy::Stream<string_source>);

// The in-memory test double reads and writes, so it models all three.
static_assert(capy::ReadStream<capy::test::stream>);
static_assert(capy::WriteStream<capy::test::stream>);
static_assert(capy::Stream<capy::test::stream>);
// end::concept_checks[]

// tag::generic_algorithm[]
// Algorithms name the concept rather than a concrete stream, so the same
// code drives a socket, a TLS stream, or the test double.
template<capy::ReadStream S>
capy::task<capy::results<std::size_t>>
count_bytes(S& source)
{
    using result = capy::results<std::size_t>;

    char storage[64];
    std::size_t total = 0;
    for(;;)
    {
        auto [ec, n] = co_await source.read_some(
            capy::make_buffer(storage));

        // Advance first, then check: a contingency can arrive together
        // with bytes, and those bytes must not be dropped.
        total += n;

        if(ec == capy::cond::eof)
            co_return result{std::error_code(), total};
        if(ec)
            co_return result{ec, total};
    }
}
// end::generic_algorithm[]

capy::task<>
count_from_string_source()
{
    string_source source("hello world");
    auto [ec, n] = co_await count_bytes(source);
    BOOST_TEST(! ec);
    BOOST_TEST(n == 11);
}

capy::task<>
count_from_test_stream(capy::test::stream& source)
{
    auto [ec, n] = co_await count_bytes(source);
    BOOST_TEST(! ec);
    BOOST_TEST(n == 4);
}

struct concepts_test
{
    void
    testCustomReadStream()
    {
        capy::test::run_blocking()(count_from_string_source());
    }

    void
    testGenericOverTestStream()
    {
        auto [a, b] = capy::test::make_stream_pair();
        b.provide("ping");
        b.close();
        capy::test::run_blocking()(count_from_test_stream(a));
    }

    void
    run()
    {
        testCustomReadStream();
        testGenericOverTestStream();
    }
};

} // namespace

TEST_SUITE(concepts_test, "boost.capy.doc.6a_concepts");
