//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/detail/except.hpp>

#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <typeinfo>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace detail {

class except_test
{
    // Run the thrower and return the message carried by the exception.
    template<class Ex, class F>
    std::string
    catch_message(F&& f)
    {
        try
        {
            f();
        }
        catch(Ex const& e)
        {
            return e.what();
        }
        return {};
    }

public:
    // The throw_* helpers are [[noreturn]], so MSVC flags the code
    // after each BOOST_TEST_THROWS expression as unreachable.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702) // unreachable code after throw
#endif
    void
    run()
    {
        BOOST_TEST_THROWS(throw_bad_typeid(), std::bad_typeid);
        BOOST_TEST_THROWS(throw_bad_alloc(), std::bad_alloc);

        BOOST_TEST_THROWS(throw_invalid_argument(), std::invalid_argument);
        BOOST_TEST_THROWS(
            throw_invalid_argument("bad"), std::invalid_argument);
        BOOST_TEST(
            catch_message<std::invalid_argument>(
                [] { throw_invalid_argument("bad"); }) == "bad");

        BOOST_TEST_THROWS(throw_length_error(), std::length_error);
        BOOST_TEST_THROWS(throw_length_error("too long"), std::length_error);
        BOOST_TEST(
            catch_message<std::length_error>(
                [] { throw_length_error("too long"); }) == "too long");

        BOOST_TEST_THROWS(throw_logic_error(), std::logic_error);
        BOOST_TEST_THROWS(throw_out_of_range(), std::out_of_range);

        BOOST_TEST_THROWS(throw_runtime_error("boom"), std::runtime_error);
        BOOST_TEST(
            catch_message<std::runtime_error>(
                [] { throw_runtime_error("boom"); }) == "boom");

        auto const ec = std::make_error_code(std::errc::invalid_argument);
        BOOST_TEST_THROWS(throw_system_error(ec), std::system_error);
        try
        {
            throw_system_error(ec);
        }
        catch(std::system_error const& e)
        {
            BOOST_TEST(e.code() == ec);
        }
    }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
};

TEST_SUITE(except_test, "boost.capy.detail.except");

} // detail
} // capy
} // boost
