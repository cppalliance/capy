//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BCRYPT_RESULT_HPP
#define BOOST_CAPY_BCRYPT_RESULT_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/bcrypt/version.hpp>
#include <boost/core/detail/string_view.hpp>
#include <boost/system/error_code.hpp>
#include <cstddef>

namespace boost {
namespace capy {
namespace bcrypt {

/** Fixed-size buffer for bcrypt hash output.

    Stores a bcrypt hash string (max 60 chars) in an
    inline buffer with no heap allocation.

    @par Example
    @code
    bcrypt::result r = bcrypt::hash("password", 10);
    core::string_view sv = r;  // or r.str()
    std::cout << r.c_str();    // null-terminated
    @endcode
*/
class result
{
    char buf_[61];          // 60 chars + null terminator
    unsigned char size_;

public:
    /** Default constructor.

        Constructs an empty result.
    */
    result() noexcept
        : size_(0)
    {
        buf_[0] = '\0';
    }

    /** Return the hash as a string_view.
    */
    core::string_view
    str() const noexcept
    {
        return core::string_view(buf_, size_);
    }

    /** Implicit conversion to string_view.
    */
    operator core::string_view() const noexcept
    {
        return str();
    }

    /** Return null-terminated C string.
    */
    char const*
    c_str() const noexcept
    {
        return buf_;
    }

    /** Return pointer to data.
    */
    char const*
    data() const noexcept
    {
        return buf_;
    }

    /** Return size in bytes (excludes null terminator).
    */
    std::size_t
    size() const noexcept
    {
        return size_;
    }

    /** Check if result is empty.
    */
    bool
    empty() const noexcept
    {
        return size_ == 0;
    }

    /** Check if result contains valid data.
    */
    explicit
    operator bool() const noexcept
    {
        return size_ != 0;
    }

private:
    friend BOOST_CAPY_DECL result gen_salt(unsigned, version);
    friend BOOST_CAPY_DECL result hash(core::string_view, unsigned, version);
    friend BOOST_CAPY_DECL result hash(core::string_view, core::string_view, system::error_code&);

    char* buf() noexcept { return buf_; }
    void set_size(unsigned char n) noexcept
    {
        size_ = n;
        buf_[n] = '\0';
    }
};

} // bcrypt
} // capy
} // boost

#endif

