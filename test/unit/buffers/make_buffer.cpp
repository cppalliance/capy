//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/make_buffer.hpp>

#include "test_suite.hpp"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// boost::span is the type reported in issue #147. It lives in Boost.Core,
// which is on the include path in the full Boost build (and CI) but not in
// the minimal standalone CMake build, so guard on availability.
#if __has_include(<boost/core/span.hpp>)
#include <boost/core/span.hpp>
#define BOOST_CAPY_TEST_HAS_BOOST_SPAN
#endif

// Statically assert the exact return type of a make_buffer(...) call.
// This is the regression guard for issue #147: a silent return-type change
// is caught at compile time. It matters most for const cases, since a
// mutable_buffer implicitly converts to const_buffer, so a const case that
// wrongly became mutable would still compile in a plain assignment.
#define CAPY_ASSERT_RETURNS(Type, ...) \
    static_assert(std::is_same_v<decltype(make_buffer(__VA_ARGS__)), Type>)

namespace boost {
namespace capy {

struct make_buffer_test
{
    void
    testMutableBuffer()
    {
        char buf[10]{};

        // make_buffer(mutable_buffer)
        {
            mutable_buffer b(buf, 10);
            CAPY_ASSERT_RETURNS(mutable_buffer, b);
            auto b1 = make_buffer(b);
            BOOST_TEST_EQ(b1.data(), b.data());
            BOOST_TEST_EQ(b1.size(), b.size());
        }

        // make_buffer(mutable_buffer, max_size) - no truncation
        {
            mutable_buffer b(buf, 10);
            CAPY_ASSERT_RETURNS(mutable_buffer, b, 20);
            auto b1 = make_buffer(b, 20);
            BOOST_TEST_EQ(b1.data(), b.data());
            BOOST_TEST_EQ(b1.size(), 10u);
        }

        // make_buffer(mutable_buffer, max_size) - truncation
        {
            mutable_buffer b(buf, 10);
            CAPY_ASSERT_RETURNS(mutable_buffer, b, 5);
            auto b1 = make_buffer(b, 5);
            BOOST_TEST_EQ(b1.data(), b.data());
            BOOST_TEST_EQ(b1.size(), 5u);
        }
    }

    void
    testConstBuffer()
    {
        char const buf[10]{};

        // make_buffer(const_buffer)
        {
            const_buffer b(buf, 10);
            CAPY_ASSERT_RETURNS(const_buffer, b);
            auto b1 = make_buffer(b);
            BOOST_TEST_EQ(b1.data(), b.data());
            BOOST_TEST_EQ(b1.size(), b.size());
        }

        // make_buffer(const_buffer, max_size) - no truncation
        {
            const_buffer b(buf, 10);
            CAPY_ASSERT_RETURNS(const_buffer, b, 20);
            auto b1 = make_buffer(b, 20);
            BOOST_TEST_EQ(b1.data(), b.data());
            BOOST_TEST_EQ(b1.size(), 10u);
        }

        // make_buffer(const_buffer, max_size) - truncation
        {
            const_buffer b(buf, 10);
            CAPY_ASSERT_RETURNS(const_buffer, b, 5);
            auto b1 = make_buffer(b, 5);
            BOOST_TEST_EQ(b1.data(), b.data());
            BOOST_TEST_EQ(b1.size(), 5u);
        }
    }

    void
    testRawPointer()
    {
        char buf[10]{};
        char* pbuf = buf;
        char const* cbuf = buf;

        // make_buffer(void*, size)
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, pbuf, 10);
            auto b = make_buffer(pbuf, 10);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(void*, size, max_size) - no truncation
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, pbuf, 10, 20);
            auto b = make_buffer(pbuf, 10, 20);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(void*, size, max_size) - truncation
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, pbuf, 10, 5);
            auto b = make_buffer(pbuf, 10, 5);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // make_buffer(void const*, size)
        {
            CAPY_ASSERT_RETURNS(const_buffer, cbuf, 10);
            auto b = make_buffer(cbuf, 10);
            BOOST_TEST_EQ(b.data(), cbuf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(void const*, size, max_size) - no truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, cbuf, 10, 20);
            auto b = make_buffer(cbuf, 10, 20);
            BOOST_TEST_EQ(b.data(), cbuf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(void const*, size, max_size) - truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, cbuf, 10, 5);
            auto b = make_buffer(cbuf, 10, 5);
            BOOST_TEST_EQ(b.data(), cbuf);
            BOOST_TEST_EQ(b.size(), 5u);
        }
    }

    void
    testCArray()
    {
        char buf[10]{};
        char const cbuf[10]{};

        // make_buffer(T(&)[N])
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, buf);
            mutable_buffer b = make_buffer(buf);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(T(&)[N], max_size) - no truncation
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, buf, 20);
            mutable_buffer b = make_buffer(buf, 20);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(T(&)[N], max_size) - truncation
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, buf, 5);
            mutable_buffer b = make_buffer(buf, 5);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // make_buffer(T const(&)[N])
        {
            CAPY_ASSERT_RETURNS(const_buffer, cbuf);
            const_buffer b = make_buffer(cbuf);
            BOOST_TEST_EQ(b.data(), cbuf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(T const(&)[N], max_size) - no truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, cbuf, 20);
            const_buffer b = make_buffer(cbuf, 20);
            BOOST_TEST_EQ(b.data(), cbuf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(T const(&)[N], max_size) - truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, cbuf, 5);
            const_buffer b = make_buffer(cbuf, 5);
            BOOST_TEST_EQ(b.data(), cbuf);
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // Multi-byte element type
        {
            int ibuf[5]{};
            CAPY_ASSERT_RETURNS(mutable_buffer, ibuf);
            mutable_buffer b = make_buffer(ibuf);
            BOOST_TEST_EQ(b.data(), ibuf);
            BOOST_TEST_EQ(b.size(), 5u * sizeof(int));
        }
    }

    void
    testStdArray()
    {
        std::array<char, 10> arr{};
        std::array<char, 10> const carr{};

        // make_buffer(std::array<T, N>&)
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, arr);
            mutable_buffer b = make_buffer(arr);
            BOOST_TEST_EQ(b.data(), arr.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::array<T, N>&, max_size) - no truncation
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, arr, 20);
            mutable_buffer b = make_buffer(arr, 20);
            BOOST_TEST_EQ(b.data(), arr.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::array<T, N>&, max_size) - truncation
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, arr, 5);
            mutable_buffer b = make_buffer(arr, 5);
            BOOST_TEST_EQ(b.data(), arr.data());
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // make_buffer(std::array<T, N> const&)
        {
            CAPY_ASSERT_RETURNS(const_buffer, carr);
            const_buffer b = make_buffer(carr);
            BOOST_TEST_EQ(b.data(), carr.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::array<T, N> const&, max_size) - no truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, carr, 20);
            const_buffer b = make_buffer(carr, 20);
            BOOST_TEST_EQ(b.data(), carr.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::array<T, N> const&, max_size) - truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, carr, 5);
            const_buffer b = make_buffer(carr, 5);
            BOOST_TEST_EQ(b.data(), carr.data());
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // Multi-byte element type
        {
            std::array<int, 5> iarr{};
            CAPY_ASSERT_RETURNS(mutable_buffer, iarr);
            mutable_buffer b = make_buffer(iarr);
            BOOST_TEST_EQ(b.data(), iarr.data());
            BOOST_TEST_EQ(b.size(), 5u * sizeof(int));
        }
    }

    void
    testStdVector()
    {
        std::vector<char> vec(10);
        std::vector<char> const cvec(10);
        std::vector<char> empty_vec;

        // make_buffer(std::vector<T>&)
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, vec);
            mutable_buffer b = make_buffer(vec);
            BOOST_TEST_EQ(b.data(), vec.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::vector<T>&, max_size) - no truncation
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, vec, 20);
            mutable_buffer b = make_buffer(vec, 20);
            BOOST_TEST_EQ(b.data(), vec.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::vector<T>&, max_size) - truncation
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, vec, 5);
            mutable_buffer b = make_buffer(vec, 5);
            BOOST_TEST_EQ(b.data(), vec.data());
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // make_buffer(std::vector<T> const&)
        {
            CAPY_ASSERT_RETURNS(const_buffer, cvec);
            const_buffer b = make_buffer(cvec);
            BOOST_TEST_EQ(b.data(), cvec.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::vector<T> const&, max_size) - no truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, cvec, 20);
            const_buffer b = make_buffer(cvec, 20);
            BOOST_TEST_EQ(b.data(), cvec.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::vector<T> const&, max_size) - truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, cvec, 5);
            const_buffer b = make_buffer(cvec, 5);
            BOOST_TEST_EQ(b.data(), cvec.data());
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // Empty vector
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, empty_vec);
            mutable_buffer b = make_buffer(empty_vec);
            BOOST_TEST_EQ(b.data(), nullptr);
            BOOST_TEST_EQ(b.size(), 0u);
        }

        // Multi-byte element type
        {
            std::vector<int> ivec(5);
            CAPY_ASSERT_RETURNS(mutable_buffer, ivec);
            mutable_buffer b = make_buffer(ivec);
            BOOST_TEST_EQ(b.data(), ivec.data());
            BOOST_TEST_EQ(b.size(), 5u * sizeof(int));
        }
    }

    void
    testStdString()
    {
        std::string str = "0123456789";
        std::string const cstr = "0123456789";
        std::string empty_str;

        // make_buffer(std::string&)
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, str);
            mutable_buffer b = make_buffer(str);
            BOOST_TEST_EQ(b.data(), &str[0]);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::string&, max_size) - no truncation
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, str, 20);
            mutable_buffer b = make_buffer(str, 20);
            BOOST_TEST_EQ(b.data(), &str[0]);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::string&, max_size) - truncation
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, str, 5);
            mutable_buffer b = make_buffer(str, 5);
            BOOST_TEST_EQ(b.data(), &str[0]);
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // make_buffer(std::string const&)
        {
            CAPY_ASSERT_RETURNS(const_buffer, cstr);
            const_buffer b = make_buffer(cstr);
            BOOST_TEST_EQ(b.data(), cstr.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::string const&, max_size) - no truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, cstr, 20);
            const_buffer b = make_buffer(cstr, 20);
            BOOST_TEST_EQ(b.data(), cstr.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::string const&, max_size) - truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, cstr, 5);
            const_buffer b = make_buffer(cstr, 5);
            BOOST_TEST_EQ(b.data(), cstr.data());
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // Empty string
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, empty_str);
            mutable_buffer b = make_buffer(empty_str);
            BOOST_TEST_EQ(b.data(), nullptr);
            BOOST_TEST_EQ(b.size(), 0u);
        }

        // Wide string
        {
            std::wstring wstr = L"hello";
            CAPY_ASSERT_RETURNS(mutable_buffer, wstr);
            mutable_buffer b = make_buffer(wstr);
            BOOST_TEST_EQ(b.data(), &wstr[0]);
            BOOST_TEST_EQ(b.size(), 5u * sizeof(wchar_t));
        }
    }

    void
    testStdStringView()
    {
        std::string_view sv = "0123456789";
        std::string_view empty_sv;

        // make_buffer(std::string_view)
        {
            CAPY_ASSERT_RETURNS(const_buffer, sv);
            const_buffer b = make_buffer(sv);
            BOOST_TEST_EQ(b.data(), sv.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::string_view, max_size) - no truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, sv, 20);
            const_buffer b = make_buffer(sv, 20);
            BOOST_TEST_EQ(b.data(), sv.data());
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::string_view, max_size) - truncation
        {
            CAPY_ASSERT_RETURNS(const_buffer, sv, 5);
            const_buffer b = make_buffer(sv, 5);
            BOOST_TEST_EQ(b.data(), sv.data());
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // Empty string_view
        {
            CAPY_ASSERT_RETURNS(const_buffer, empty_sv);
            const_buffer b = make_buffer(empty_sv);
            BOOST_TEST_EQ(b.data(), nullptr);
            BOOST_TEST_EQ(b.size(), 0u);
        }

        // Wide string_view
        {
            std::wstring_view wsv = L"hello";
            CAPY_ASSERT_RETURNS(const_buffer, wsv);
            const_buffer b = make_buffer(wsv);
            BOOST_TEST_EQ(b.data(), wsv.data());
            BOOST_TEST_EQ(b.size(), 5u * sizeof(wchar_t));
        }
    }

    void
    testStdSpan()
    {
        char buf[10]{};
        char const cbuf[10]{};

        // make_buffer(std::span<T>) - mutable
        {
            std::span<char> sp(buf);
            CAPY_ASSERT_RETURNS(mutable_buffer, sp);
            mutable_buffer b = make_buffer(sp);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::span<T>) - mutable, passed as a temporary.
        // Regression: a prvalue must still yield a mutable_buffer once
        // the dedicated by-value span overload is gone.
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, std::span<char>(buf));
            mutable_buffer b = make_buffer(std::span<char>(buf));
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::span<T>, max_size) - no truncation
        {
            std::span<char> sp(buf);
            CAPY_ASSERT_RETURNS(mutable_buffer, sp, 20);
            mutable_buffer b = make_buffer(sp, 20);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::span<T>, max_size) - truncation
        {
            std::span<char> sp(buf);
            CAPY_ASSERT_RETURNS(mutable_buffer, sp, 5);
            mutable_buffer b = make_buffer(sp, 5);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // make_buffer(std::span<T const>) - const
        {
            std::span<char const> sp(cbuf);
            CAPY_ASSERT_RETURNS(const_buffer, sp);
            const_buffer b = make_buffer(sp);
            BOOST_TEST_EQ(b.data(), cbuf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::span<T const>, max_size) - no truncation
        {
            std::span<char const> sp(cbuf);
            CAPY_ASSERT_RETURNS(const_buffer, sp, 20);
            const_buffer b = make_buffer(sp, 20);
            BOOST_TEST_EQ(b.data(), cbuf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // make_buffer(std::span<T const>, max_size) - truncation
        {
            std::span<char const> sp(cbuf);
            CAPY_ASSERT_RETURNS(const_buffer, sp, 5);
            const_buffer b = make_buffer(sp, 5);
            BOOST_TEST_EQ(b.data(), cbuf);
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // Fixed-extent span
        {
            std::span<char, 10> sp(buf);
            CAPY_ASSERT_RETURNS(mutable_buffer, sp);
            mutable_buffer b = make_buffer(sp);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 10u);
        }
    }

#ifdef BOOST_CAPY_TEST_HAS_BOOST_SPAN
    void
    testBoostSpan()
    {
        char buf[10]{};
        char const cbuf[10]{};

        // Regression for issue #147: make_buffer(boost::span<char>) must
        // return a mutable_buffer. boost::span has no dedicated overload,
        // so it exercises the generic forwarding-reference overload.

        // boost::span<char> passed as a temporary (the reported case)
        {
            CAPY_ASSERT_RETURNS(mutable_buffer, boost::span<char>(buf));
            mutable_buffer b = make_buffer(boost::span<char>(buf));
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // boost::span<char> lvalue
        {
            boost::span<char> sp(buf);
            CAPY_ASSERT_RETURNS(mutable_buffer, sp);
            mutable_buffer b = make_buffer(sp);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 10u);
        }

        // boost::span<char> with max_size - truncation
        {
            boost::span<char> sp(buf);
            CAPY_ASSERT_RETURNS(mutable_buffer, sp, 5);
            mutable_buffer b = make_buffer(sp, 5);
            BOOST_TEST_EQ(b.data(), buf);
            BOOST_TEST_EQ(b.size(), 5u);
        }

        // boost::span<char const> -> const_buffer
        {
            boost::span<char const> sp(cbuf);
            CAPY_ASSERT_RETURNS(const_buffer, sp);
            const_buffer b = make_buffer(sp);
            BOOST_TEST_EQ(b.data(), cbuf);
            BOOST_TEST_EQ(b.size(), 10u);
        }
    }
#endif

    void
    testStringLiteral()
    {
        // A string literal is a const char array; it must resolve to the
        // const range overload (not string_view) and include the trailing
        // '\0', matching the prior dedicated C-array overload.
        CAPY_ASSERT_RETURNS(const_buffer, "Hello");
        const_buffer b = make_buffer("Hello");
        BOOST_TEST_EQ(b.size(), 6u);
    }

    void
    run()
    {
        testMutableBuffer();
        testConstBuffer();
        testRawPointer();
        testCArray();
        testStdArray();
        testStdVector();
        testStdString();
        testStdStringView();
        testStdSpan();
#ifdef BOOST_CAPY_TEST_HAS_BOOST_SPAN
        testBoostSpan();
#endif
        testStringLiteral();
    }
};

TEST_SUITE(
    make_buffer_test,
    "boost.capy.buffers.make_buffer");

} // capy
} // boost

#undef CAPY_ASSERT_RETURNS
