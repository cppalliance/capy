//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/5.buffers/5a.contiguous-buffers.adoc.

#include "../doc_warnings.hpp"

#include <boost/capy/buffers.hpp>
#include <boost/capy/io/any_read_stream.hpp>
#include <boost/capy/io/any_write_stream.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/stream.hpp>
// tag::make_buffer_hpp[]
#include <boost/capy/buffers/make_buffer.hpp>
// end::make_buffer_hpp[]
#include <boost/capy/write.hpp>

#include <array>
#include <span>
#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if __has_include(<sys/mman.h>)
#include <sys/mman.h>
#endif

#include "test_suite.hpp"

namespace capy = boost::capy;

namespace {


#if __has_include(<sys/mman.h>)

template <typename F>
struct scope_guard
{
  F fun;
  scope_guard(F f) : fun(std::move(f)) {}
  scope_guard(scope_guard&&) = delete;
  ~scope_guard() { fun(); }
};

[[maybe_unused]] capy::task<>
send_mapped_file(capy::test::stream& socket, int fd, std::size_t file_size)
{
  // tag::mmap_buffer[]
  void* mapped = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0); //<1>
  if (mapped != MAP_FAILED) {
    scope_guard _ = [&]{ ::munmap(mapped, file_size); };  //<2>

    capy::const_buffer file_buf(mapped, file_size);       //<3>
    co_await capy::write(socket, file_buf);               //<4>
  }
  // end::mmap_buffer[]
}

#endif // __has_include(<sys/mman.h>)


struct contiguous_buffers_test
{
    void
    testConstruct()
    {
        // tag::construct[]
        std::string_view header = "Content-Type: application/json";
        capy::const_buffer buf(header.data(), header.size());  // <1>
        capy::const_buffer buf2 = buf;                         // <2>

        assert(buf.data() == header.data());
        assert(buf2.data() == header.data());
        // end::construct[]

        BOOST_TEST(buf.data() == header.data());
        BOOST_TEST(buf2.data() == header.data());
        BOOST_TEST(buf.size() == header.size());
        BOOST_TEST(buf2.size() == header.size());
    }

    void
    testTypes()
    {
        // tag::types[]
        char carray [1024];
        capy::mutable_buffer buf1(carray, sizeof(carray));       // <1>

        std::array<std::byte, 1024> barray;
        capy::mutable_buffer buf2(barray.data(), barray.size()); // <2>

        std::vector<int> iarray(256);
        capy::mutable_buffer buf3(iarray.data(), iarray.size() * sizeof(int)); // <3>
        // end::types[]

        BOOST_TEST(buf3.size() == 256 * sizeof(int));
    }

    void process(char*, std::size_t) {}

    capy::task<> goodManagement(capy::any_read_stream &stream)
    {
        // tag::good_management[]
        char storage[1024];
        capy::mutable_buffer buf(storage, sizeof(storage));
        auto [ec, n] = co_await stream.read_some(buf);  // <1>
        process(storage, n);
        // end::good_management[]

        co_return;
    }

    capy::task<> wrongBuffer(capy::ReadStream auto& stream)
    {
        // tag::wrong_buffer[]
        std::string_view header = "Content-Type: application/json";
        capy::const_buffer buf(header.data(), header.size());
        auto [ec, n] = co_await stream.read_some(buf);  // <1>
        // end::wrong_buffer[]

        co_return;
    }

    capy::task<> testBufferConversion(capy::any_write_stream &stream)
    {
        // tag::buffer_conversion[]
        char storage[1024];                               // <1>
        capy::mutable_buffer mbuf(storage, sizeof(storage));
        auto [ec, n] = co_await stream.write_some(mbuf);  // <2>
        // end::buffer_conversion[]

        co_return;
    }


    auto badManagement(capy::any_read_stream &stream)
    {
        // tag::bad_management[]
        char storage[1024];
        capy::mutable_buffer buf(storage, sizeof(storage));
        return stream.read_some(buf);  // <1>
        // end::bad_management[]
    }

    // tag::buffer_interface[]
    void buffer_interface(capy::const_buffer   cbuf, 
                          capy::mutable_buffer mbuf)
    {
      std::size_t len  = cbuf.size();  // <1>
      std::size_t mlen = mbuf.size();

      void const* ptr = cbuf.data();   // <2>
      void*      mptr = mbuf.data();

      mbuf += len;                     // <3>

      assert(mbuf.data() == (char*)mptr + len);
      assert(mbuf.size() == mlen - len);
    }
    // end::buffer_interface[]

    void testInterface()
    {
        char csmall [20] = {};
        capy::const_buffer cbuf (csmall, sizeof(csmall));
        
        char mbig [40] = {};
        capy::mutable_buffer mbuf(mbig, sizeof(mbig));

        buffer_interface(cbuf, mbuf);
    }

    void testMakeBuffer()
    {
      char arr[10];
      {
        // tag::buf_from_carray[]
        char arr[10];
        capy::mutable_buffer buf = capy::make_buffer(arr);
        // end::buf_from_carray[]
      }
      {
        // tag::buf_from_std_array[]
        std::array<std::byte, 10> std_arr;
        capy::mutable_buffer buf = capy::make_buffer(std_arr);
        // end::buf_from_std_array[]
      }
      {
        // tag::buf_from_vec[]
        std::vector<int> vec(100);
        capy::mutable_buffer buf = capy::make_buffer(vec);
        // end::buf_from_vec[]
      }
      {
        // tag::buf_from_str[]
        std::string str = "message";
        capy::mutable_buffer buf = capy::make_buffer(str);
        // end::buf_from_str[]
      }
      {
        // tag::buf_from_strv[]
        std::string_view str = "message";
        capy::const_buffer buf = capy::make_buffer(str);
        // end::buf_from_strv[]
      }  
      {
        // tag::buf_from_span[]
        std::span<char> sp(arr); // or boost::span
        capy::mutable_buffer buf = capy::make_buffer(sp);
        // end::buf_from_span[]
      } 
      {
        // tag::buf_from_nested[]
        std::array<std::array<char, 4>, 100> array2;
        capy::mutable_buffer buf = capy::make_buffer(array2);
        assert(buf.size() == 400);
        // end::buf_from_nested[]
      } 
    }

    void
    run()
    {
        testConstruct();
        testTypes();
        testInterface();
        testMakeBuffer();
    }
};

} // namespace

TEST_SUITE(contiguous_buffers_test, "boost.capy.doc.5a_contiguous_buffers");
