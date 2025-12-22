//
// Copyright (c) 2022 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/detail/file_win32.hpp>

#if BOOST_CAPY_USE_WIN32_FILE

#include "src/detail/win32_unicode_path.hpp"
#include <boost/core/exchange.hpp>
#include <boost/system/errc.hpp>
#include <boost/winapi/access_rights.hpp>
#include <boost/winapi/error_codes.hpp>
#include <boost/winapi/get_last_error.hpp>
#include <limits>
#include <utility>

namespace boost {
namespace capy {
namespace detail {

// VFALCO Can't seem to get boost/detail/winapi to work with
//        this so use the non-Ex version for now.
BOOST_CAPY_DECL
winapi::BOOL_
set_file_pointer_ex(
    winapi::HANDLE_ hFile,
    winapi::LARGE_INTEGER_ lpDistanceToMove,
    winapi::PLARGE_INTEGER_ lpNewFilePointer,
    winapi::DWORD_ dwMoveMethod)
{
    auto dwHighPart = lpDistanceToMove.u.HighPart;
    auto dwLowPart = winapi::SetFilePointer(
        hFile,
        lpDistanceToMove.u.LowPart,
        &dwHighPart,
        dwMoveMethod);
    if(dwLowPart == winapi::INVALID_SET_FILE_POINTER_)
        return 0;
    if(lpNewFilePointer)
    {
        lpNewFilePointer->u.LowPart = dwLowPart;
        lpNewFilePointer->u.HighPart = dwHighPart;
    }
    return 1;
}

file_win32::
~file_win32()
{
    if(h_ != winapi::INVALID_HANDLE_VALUE_)
        winapi::CloseHandle(h_);
}

file_win32::
file_win32(
    file_win32&& other) noexcept
    : h_(boost::exchange(other.h_,
        winapi::INVALID_HANDLE_VALUE_))
{
}

file_win32&
file_win32::
operator=(
    file_win32&& other) noexcept
{
    if(&other == this)
        return *this;
    if(h_)
        winapi::CloseHandle(h_);
    h_ = other.h_;
    other.h_ = winapi::INVALID_HANDLE_VALUE_;
    return *this;
}

void
file_win32::
native_handle(native_handle_type h)
{
     if(h_ != winapi::INVALID_HANDLE_VALUE_)
        winapi::CloseHandle(h_);
    h_ = h;
}

void
file_win32::
close(
    system::error_code& ec)
{
    if(h_ != winapi::INVALID_HANDLE_VALUE_)
    {
        if(! winapi::CloseHandle(h_))
            ec.assign(
                winapi::GetLastError(),
                system::system_category());
        else
            ec = {};
        h_ = winapi::INVALID_HANDLE_VALUE_;
    }
    else
    {
        ec = {};
    }
}

void
file_win32::
open(char const* path, file_mode mode,
    system::error_code& ec)
{
    if(h_ != winapi::INVALID_HANDLE_VALUE_)
    {
        winapi::CloseHandle(h_);
        h_ = winapi::INVALID_HANDLE_VALUE_;
    }
    winapi::DWORD_ share_mode = 0;
    winapi::DWORD_ desired_access = 0;
    winapi::DWORD_ creation_disposition = 0;
    winapi::DWORD_ flags_and_attributes = 0;
/*
                             |                    When the file...
    This argument:           |             Exists            Does not exist
    -------------------------+------------------------------------------------------
    CREATE_ALWAYS            |            Truncates             Creates
    CREATE_NEW         +-----------+        Fails               Creates
    OPEN_ALWAYS     ===| does this |===>    Opens               Creates
    OPEN_EXISTING      +-----------+        Opens                Fails
    TRUNCATE_EXISTING        |            Truncates              Fails
*/
    switch(mode)
    {
    default:
    case file_mode::read:
        desired_access = winapi::GENERIC_READ_;
        share_mode = winapi::FILE_SHARE_READ_;
        creation_disposition = winapi::OPEN_EXISTING_;
        flags_and_attributes = 0x10000000; // FILE_FLAG_RANDOM_ACCESS
        break;

    case file_mode::scan:           
        desired_access = winapi::GENERIC_READ_;
        share_mode = winapi::FILE_SHARE_READ_;
        creation_disposition = winapi::OPEN_EXISTING_;
        flags_and_attributes = 0x08000000; // FILE_FLAG_SEQUENTIAL_SCAN
        break;

    case file_mode::write:          
        desired_access = winapi::GENERIC_READ_ |
                         winapi::GENERIC_WRITE_;
        creation_disposition = winapi::CREATE_ALWAYS_;
        flags_and_attributes = 0x10000000; // FILE_FLAG_RANDOM_ACCESS
        break;

    case file_mode::write_new:      
        desired_access = winapi::GENERIC_READ_ |
                         winapi::GENERIC_WRITE_;
        creation_disposition = winapi::CREATE_NEW_;
        flags_and_attributes = 0x10000000; // FILE_FLAG_RANDOM_ACCESS
        break;

    case file_mode::write_existing: 
        desired_access = winapi::GENERIC_READ_ |
                         winapi::GENERIC_WRITE_;
        creation_disposition = winapi::OPEN_EXISTING_;
        flags_and_attributes = 0x10000000; // FILE_FLAG_RANDOM_ACCESS
        break;

    case file_mode::append:         
        desired_access = winapi::GENERIC_READ_ |
                         winapi::GENERIC_WRITE_;

        creation_disposition = winapi::OPEN_ALWAYS_;
        flags_and_attributes = 0x08000000; // FILE_FLAG_SEQUENTIAL_SCAN
        break;

    case file_mode::append_existing:
        desired_access = winapi::GENERIC_READ_ |
                         winapi::GENERIC_WRITE_;
        creation_disposition = winapi::OPEN_EXISTING_;
        flags_and_attributes = 0x08000000; // FILE_FLAG_SEQUENTIAL_SCAN
        break;
    }
    
    detail::win32_unicode_path unicode_path(path, ec);
    if (ec)
        return;
    h_ = ::CreateFileW(
        unicode_path.c_str(),
        desired_access,
        share_mode,
        NULL,
        creation_disposition,
        flags_and_attributes,
        NULL);
    if (h_ == winapi::INVALID_HANDLE_VALUE_)
    {
        ec.assign(winapi::GetLastError(),
            system::system_category());
        return;
    }
    if (mode == file_mode::append ||
        mode == file_mode::append_existing)
    {
        winapi::LARGE_INTEGER_ in;
        in.QuadPart = 0;
        if (!detail::set_file_pointer_ex(h_, in, 0,
            winapi::FILE_END_))
        {
            ec.assign(winapi::GetLastError(),
                system::system_category());
            winapi::CloseHandle(h_);
            h_ = winapi::INVALID_HANDLE_VALUE_;
            return;
        }
    }
    ec = {};
}

std::uint64_t
file_win32::
size(
    system::error_code& ec) const
{
    if(h_ == winapi::INVALID_HANDLE_VALUE_)
    {
        ec = make_error_code(
            system::errc::bad_file_descriptor);
        return 0;
    }
    winapi::LARGE_INTEGER_ fileSize;
    if(! winapi::GetFileSizeEx(h_, &fileSize))
    {
        ec.assign(winapi::GetLastError(),
            system::system_category());
        return 0;
    }
    ec = {};
    return fileSize.QuadPart;
}

std::uint64_t
file_win32::
pos(
    system::error_code& ec) const
{
    if(h_ == winapi::INVALID_HANDLE_VALUE_)
    {
        ec = make_error_code(
            system::errc::bad_file_descriptor);
        return 0;
    }
    winapi::LARGE_INTEGER_ in;
    winapi::LARGE_INTEGER_ out;
    in.QuadPart = 0;
    if(! detail::set_file_pointer_ex(h_, in, &out,
        winapi::FILE_CURRENT_))
    {
        ec.assign(winapi::GetLastError(),
            system::system_category());
        return 0;
    }
    ec = {};
    return out.QuadPart;
}

void
file_win32::
seek(std::uint64_t offset,
    system::error_code& ec)
{
    if(h_ == winapi::INVALID_HANDLE_VALUE_)
    {
        ec = make_error_code(
            system::errc::bad_file_descriptor);
        return;
    }
    winapi::LARGE_INTEGER_ in;
    in.QuadPart = offset;
    if(! detail::set_file_pointer_ex(h_, in, 0,
        winapi::FILE_BEGIN_))
    {
        ec.assign(winapi::GetLastError(),
            system::system_category());
        return;
    }
    ec = {};
}

std::size_t
file_win32::
read(void* buffer, std::size_t n,
    system::error_code& ec)
{
    if(h_ == winapi::INVALID_HANDLE_VALUE_)
    {
        ec = make_error_code(
            system::errc::bad_file_descriptor);
        return 0;
    }
    std::size_t nread = 0;
    while(n > 0)
    {
        winapi::DWORD_ amount;
        if(n > (std::numeric_limits<
                winapi::DWORD_>::max)())
            amount = (std::numeric_limits<
                winapi::DWORD_>::max)();
        else
            amount = static_cast<
                winapi::DWORD_>(n);
        winapi::DWORD_ bytesRead;
        if(! ::ReadFile(h_, buffer, amount, &bytesRead, 0))
        {
            auto const dwError = winapi::GetLastError();
            if(dwError != winapi::ERROR_HANDLE_EOF_)
                ec.assign(dwError,
                    system::system_category());
            else
                ec = {};
            return nread;
        }
        if(bytesRead == 0)
            return nread;
        n -= bytesRead;
        nread += bytesRead;
        buffer = static_cast<char*>(buffer) + bytesRead;
    }
    ec = {};
    return nread;
}

std::size_t
file_win32::
write(void const* buffer, std::size_t n,
    system::error_code& ec)
{
    if(h_ == winapi::INVALID_HANDLE_VALUE_)
    {
        ec = make_error_code(
            system::errc::bad_file_descriptor);
        return 0;
    }
    std::size_t nwritten = 0;
    while(n > 0)
    {
        winapi::DWORD_ amount;
        if(n > (std::numeric_limits<
                winapi::DWORD_>::max)())
            amount = (std::numeric_limits<
                winapi::DWORD_>::max)();
        else
            amount = static_cast<
                winapi::DWORD_>(n);
        winapi::DWORD_ bytesWritten;
        if(! ::WriteFile(h_, buffer, amount, &bytesWritten, 0))
        {
            auto const dwError = winapi::GetLastError();
            if(dwError != winapi::ERROR_HANDLE_EOF_)
                ec.assign(dwError,
                    system::system_category());
            else
                ec = {};
            return nwritten;
        }
        if(bytesWritten == 0)
            return nwritten;
        n -= bytesWritten;
        nwritten += bytesWritten;
        buffer = static_cast<char const*>(buffer) + bytesWritten;
    }
    ec = {};
    return nwritten;
}

} // detail
} // capy
} // boost

#endif
