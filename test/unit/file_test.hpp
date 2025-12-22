//
// Copyright (c) 2022 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_HTTP_PROTO_FILE_TEST_HPP
#define BOOST_HTTP_PROTO_FILE_TEST_HPP

#include <boost/capy/file_mode.hpp>
#include <boost/config.hpp>
#include <boost/core/detail/string_view.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/static_assert.hpp>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <type_traits>
#include "test_suite.hpp"

#if defined(BOOST_GCC) && BOOST_GCC >= 130000
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif

namespace boost {
namespace capy {

template<class File, bool append_unicode_suffix = false>
void
test_file()
{
    BOOST_STATIC_ASSERT(
        ! std::is_copy_constructible<File>::value);
    BOOST_STATIC_ASSERT(
        ! std::is_copy_assignable<File>::value);

    namespace fs = boost::filesystem;

#ifdef _WIN32
    static
    constexpr
    boost::winapi::WCHAR_
    unicode_suffix[] = {
        0xd83e, 0xdd84, 0x0000 }; // UTF-16-LE unicorn
#else
    static
    constexpr
    char
    unicode_suffix[] = {
        '\xf0', '\x9f', '\xa6', '\x84', '\x00' }; // UTF-8 unicorn
#endif

    class temp_path
    {
        fs::path path_;
        std::vector<char> utf8_str_;

    public:
        temp_path()
            : path_(fs::unique_path())
        {
            if (append_unicode_suffix)
                path_ += unicode_suffix;
#ifdef _WIN32
            constexpr auto cp = boost::winapi::CP_UTF8_;
            constexpr auto flags = boost::winapi::WC_ERR_INVALID_CHARS_;
            auto sz = boost::winapi::WideCharToMultiByte(
                cp, flags, path_.c_str(), -1, nullptr, 0,
                nullptr, nullptr);
            BOOST_TEST(sz != 0);
            utf8_str_.resize(sz);
            auto ret = boost::winapi::WideCharToMultiByte(
                cp, flags, path_.c_str(), -1,
                utf8_str_.data(), sz,
                nullptr, nullptr);
            BOOST_TEST(ret == sz);
#endif
        }

        operator fs::path const&()
        {
            return path_;
        }

        operator char const*()
        {
#ifdef _WIN32
            return utf8_str_.data();
#else
            return path_.c_str();
#endif
        }
    };

    auto const create =
        [](fs::path const& path, std::string const& data = "")
        {
            BOOST_TEST(! fs::exists(path));
            std::ofstream out(path.c_str());
            BOOST_TEST(out.is_open());
            if (data.size())
                out.write(data.c_str(), data.size());
        };

    auto const remove =
        [](fs::path const& path)
        {
            fs::remove(path);
            BOOST_TEST(! fs::exists(path));
        };

    auto const consume_file =
        [](fs::path const& path)
        {
            // no exceptions - failure will result in an empty string
            std::ifstream in(path.c_str());
            noskipws(in);
            auto s = std::string(
                std::istream_iterator<char>(in),
                std::istream_iterator<char>());
            in.close();
            return s;
        };

    temp_path path;

    // bad file descriptor
    {
        File f;
        char buf[1];
        BOOST_TEST(! f.is_open());
        BOOST_TEST(! fs::exists(path));
        {
            system::error_code ec;
            f.size(ec);
            BOOST_TEST(ec ==
                system::errc::bad_file_descriptor);
        }
        {
            system::error_code ec;
            f.pos(ec);
            BOOST_TEST(ec ==
                system::errc::bad_file_descriptor);
        }
        {
            system::error_code ec;
            f.seek(0, ec);
            BOOST_TEST(ec ==
                system::errc::bad_file_descriptor);
        }
        {
            system::error_code ec;
            f.read(buf, 0, ec);
            BOOST_TEST(ec ==
                system::errc::bad_file_descriptor);
        }
        {
            system::error_code ec;
            f.write(buf, 0, ec);
            BOOST_TEST(ec ==
                system::errc::bad_file_descriptor);
        }
    }

    // file_mode::read
    {
        {
            File f;
            system::error_code ec;
            create(path);
            f.open(path, file_mode::read, ec);
            BOOST_TEST(! ec);
        }
        remove(path);
    }

    // file_mode::scan
    {
        {
            File f;
            system::error_code ec;
            create(path);
            f.open(path, file_mode::scan, ec);
            BOOST_TEST(! ec);
        }
        remove(path);
    }

    // file_mode::write
    {
        {
            File f;
            system::error_code ec;
            BOOST_TEST(! fs::exists(path));
            f.open(path, file_mode::write, ec);
            BOOST_TEST(! ec);
            BOOST_TEST(fs::exists(path));
        }
        {
            File f;
            system::error_code ec;
            BOOST_TEST(fs::exists(path));
            f.open(path, file_mode::write, ec);
            BOOST_TEST(! ec);
            BOOST_TEST(fs::exists(path));
        }
        remove(path);
    }

    // file_mode::write_new
    {
        {
            File f;
            system::error_code ec;
            BOOST_TEST(! fs::exists(path));
            f.open(path, file_mode::write_new, ec);
            BOOST_TEST(! ec);
            BOOST_TEST(fs::exists(path));
        }
        {
            File f;
            system::error_code ec;
            BOOST_TEST(fs::exists(path));
            f.open(path, file_mode::write_new, ec);
            BOOST_TEST(ec);
        }
        remove(path);
    }

    // file_mode::write_existing
    {
        {
            File f;
            system::error_code ec;
            BOOST_TEST(! fs::exists(path));
            f.open(path, file_mode::write_existing, ec);
            BOOST_TEST(ec);
            BOOST_TEST(! fs::exists(path));
        }
        {
            File f;
            system::error_code ec;
            create(path);
            BOOST_TEST(fs::exists(path));
            f.open(path, file_mode::write_existing, ec);
            BOOST_TEST(! ec);
        }
        remove(path);
    }

    // file_mode::append
    {
        {
            File f;
            system::error_code ec;
            BOOST_TEST(! fs::exists(path));
            f.open(path, file_mode::append, ec);
            BOOST_TEST(! ec);
            BOOST_TEST(fs::exists(path));
            static const std::string extra = "the";
            f.write(extra.c_str(), extra.size(), ec);
            BOOST_TEST(!ec);
            f.close(ec);
            auto s = consume_file(path);
            BOOST_TEST(s == "the");
        }

        {
            File f;
            system::error_code ec;
            BOOST_TEST(fs::exists(path));
            f.open(path, file_mode::append, ec);
            BOOST_TEST(! ec);
            BOOST_TEST(fs::exists(path));
            static const std::string extra = " cat";
            f.write(extra.c_str(), extra.size(), ec);
            BOOST_TEST(!ec);
            f.close(ec);
            auto s = consume_file(path);
            BOOST_TEST_EQ(s, "the cat");
        }
        remove(path);
    }

    // file_mode::append_existing
    {
        {
            File f;
            system::error_code ec;
            BOOST_TEST(! fs::exists(path));
            f.open(path, file_mode::append_existing, ec);
            BOOST_TEST(ec);
            BOOST_TEST(! fs::exists(path));
        }
        remove(path);
        {
            File f;
            system::error_code ec;
            create(path, "the cat");
            f.open(path, file_mode::append_existing, ec);
            BOOST_TEST(! ec);
            static std::string const extra = " sat";
            f.write(extra.c_str(), extra.size(), ec);
            BOOST_TEST(!ec);
            f.close(ec);
            BOOST_TEST(!ec);
            auto s = consume_file(path);
            BOOST_TEST_EQ(s, "the cat sat");
        }
        remove(path);
    }

    // special members
    {
        {
            File f1;
            system::error_code ec;
            f1.open(path, file_mode::write, ec);
            BOOST_TEST(! ec);
            BOOST_TEST(f1.is_open());

            // move constructor
            File f2(std::move(f1));
            BOOST_TEST(! f1.is_open());
            BOOST_TEST(f2.is_open());

            // move assignment
            File f3;
            f3 = std::move(f2);
            BOOST_TEST(! f2.is_open());
            BOOST_TEST(f3.is_open());
        }
        remove(path);
    }

    // re-open
    {
        {
            File f;
            system::error_code ec;
            f.open(path, file_mode::write, ec);
            BOOST_TEST(! ec);
            f.open(path, file_mode::write, ec);
            BOOST_TEST(! ec);
        }
        remove(path);
    }

    // re-assign
    {
        temp_path path2;
        {
            system::error_code ec;

            File f1;
            f1.open(path, file_mode::write, ec);
            BOOST_TEST(! ec);

            File f2;
            f2.open(path2, file_mode::write, ec);
            BOOST_TEST(! ec);

            f2 = std::move(f1);
            BOOST_TEST(! f1.is_open());
            BOOST_TEST(f2.is_open());
        }
        remove(path);
        remove(path2);
    }

    // self-move
    {
        {
            File f;
            system::error_code ec;
            f.open(path, file_mode::write, ec);
            BOOST_TEST(! ec);
            auto& f_(f);
            f_ = std::move(f);
            BOOST_TEST(f.is_open());
        }
        remove(path);
    }

    // native_handle
    {
        {
            File f;
            auto none = f.native_handle();
            system::error_code ec;
            f.open(path, file_mode::write, ec);
            BOOST_TEST(! ec);
            auto fd = f.native_handle();
            BOOST_TEST(fd != none);
            f.native_handle(none);
            BOOST_TEST(! f.is_open());
        }
        remove(path);
    }

    // read and write
    {
        core::string_view const s =
            "Hello, world!";

        // write
        {
            File f;
            system::error_code ec;
            f.open(path, file_mode::write, ec);
            BOOST_TEST(! ec);

            f.write(s.data(), s.size(), ec);
            BOOST_TEST(! ec);

            auto size = f.size(ec);
            BOOST_TEST(! ec);
            BOOST_TEST(size == s.size());

            auto pos = f.pos(ec);
            BOOST_TEST(! ec);
            BOOST_TEST(pos == size);

            f.close(ec);
            BOOST_TEST(! ec);
        }

        // read
        {
            File f;
            system::error_code ec;
            f.open(path, file_mode::read, ec);
            BOOST_TEST(! ec);

            std::string buf;
            buf.resize(s.size());
            f.read(&buf[0], buf.size(), ec);
            BOOST_TEST(! ec);
            BOOST_TEST(buf == s);

            f.seek(1, ec);
            BOOST_TEST(! ec);
            buf.resize(3);
            f.read(&buf[0], buf.size(), ec);
            BOOST_TEST(! ec);
            BOOST_TEST(buf == "ell");

            auto pos = f.pos(ec);
            BOOST_TEST(! ec);
            BOOST_TEST(pos == 4);
        }
        remove(path);
    }

    BOOST_TEST(! fs::exists(path));
}

} // capy
} // boost

#endif
