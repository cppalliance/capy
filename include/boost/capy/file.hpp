//
// Copyright (c) 2022 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_FILE_HPP
#define BOOST_CAPY_FILE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/detail/except.hpp>
#include <boost/capy/detail/file_posix.hpp>
#include <boost/capy/detail/file_stdio.hpp>
#include <boost/capy/detail/file_win32.hpp>
#include <boost/capy/file_mode.hpp>

namespace boost {
namespace capy {

/** A platform-independent file stream.

    This class provides a portable interface for
    reading from and writing to files. 
*/
class file
{
#if BOOST_CAPY_USE_WIN32_FILE
    using impl_type = detail::file_win32;
#elif BOOST_CAPY_USE_POSIX_FILE
    using impl_type = detail::file_posix;
#else
    using impl_type = detail::file_stdio;
#endif

    impl_type impl_;

public:
    /** The type of the underlying native file handle.

        This type is platform-specific.
    */
    using native_handle_type = impl_type::native_handle_type;

    /** Constructor.

        There is no open file initially.
    */
    file() = default;

    /** Constructor.

        Open a file at the given path with the specified mode.

        @par Exception Safety
        Exception thrown if operation fails.

        @throw system_error
        Operation fails.

        @param path The UTF-8 encoded path to the file.

        @param mode The file mode to use.

        @see
            @ref file_mode,
            @ref open.
    */
    file(char const* path, file_mode mode)
    {
        open(path, mode);
    }

    /** Constructor.

        The moved-from object behaves as if default-constructed.
    */
    file(file&& other) noexcept = default;

    /** Assignment

        The moved-from object behaves as if default-constructed.
    */
    file&
    operator=(
        file&& other) noexcept = default;

    /** Destructor

        If the file is open it is first closed.
    */
    ~file() = default;

    /** Returns the native handle associated with the file.
    */
    native_handle_type
    native_handle()
    {
        return impl_.native_handle();
    }

    /** Set the native file handle.

        If the file is open it is first closed.

        @param h The native handle to assign.
    */
    void
    native_handle(native_handle_type h)
    {
        impl_.native_handle(h);
    }

    /** Return true if the file is open.
    */
    bool
    is_open() const
    {
        return impl_.is_open();
    }

    /** Close the file if open.

        Note that, The descriptor is closed even if the function
        reports an error.

        @param ec Set to the error, if any occurred.
    */
    void
    close(system::error_code& ec)
    {
        impl_.close(ec);
    }

    /** Close the file if open.

        Note that, The descriptor is closed even if the function
        reports an error.

        @par Exception Safety
        Exception thrown if operation fails.

        @throw system_error
        Operation fails.
    */
    void
    close()
    {
        system::error_code ec;
        impl_.close(ec);
        if(ec.failed())
            detail::throw_system_error(ec);
    }

    /** Open a file at the given path with the specified mode.

        @param path The UTF-8 encoded path to the file.

        @param mode The file mode to use.

        @param ec Set to the error, if any occurred.

        @see
            @ref file_mode.
    */
    void
    open(char const* path, file_mode mode, system::error_code& ec)
    {
        impl_.open(path, mode, ec);
    }

    /** Open a file at the given path with the specified mode.

        @param path The UTF-8 encoded path to the file.

        @param mode The file mode to use.

        @par Exception Safety
        Exception thrown if operation fails.

        @throw system_error
        Operation fails.

        @see
            @ref file_mode.
    */
    void
    open(char const* path, file_mode mode)
    {
        system::error_code ec;
        impl_.open(path, mode, ec);
        if(ec.failed())
            detail::throw_system_error(ec);
    }

    /** Return the size of the open file in bytes.

        @param ec Set to the error, if any occurred.
    */
    std::uint64_t
    size(system::error_code& ec) const
    {
        return impl_.size(ec);
    }

    /** Return the size of the open file in bytes.

        @par Exception Safety
        Exception thrown if operation fails.

        @throw system_error
        Operation fails.
    */
    std::uint64_t
    size() const
    {
        system::error_code ec;
        auto r = impl_.size(ec);
        if(ec.failed())
            detail::throw_system_error(ec);
        return r;
    }

    /** Return the current position in the file, in bytes from the beginning.

        @param ec Set to the error, if any occurred.
    */
    std::uint64_t
    pos(system::error_code& ec) const
    {
        return impl_.pos(ec);
    }

    /** Return the current position in the file, in bytes from the beginning.

        @par Exception Safety
        Exception thrown if operation fails.

        @throw system_error
        Operation fails.
    */
    std::uint64_t
    pos() const
    {
        system::error_code ec;
        auto r = impl_.pos(ec);
        if(ec.failed())
            detail::throw_system_error(ec);
        return r;
    }

    /** Set the current position in the file.

        @param offset The byte offset from the beginning of the file.

        @param ec Set to the error, if any occurred.
    */
    void
    seek(std::uint64_t offset, system::error_code& ec)
    {
        impl_.seek(offset, ec);
    }

    /** Set the current position in the file.

        @par Exception Safety
        Exception thrown if operation fails.

        @throw system_error
        Operation fails.

        @param offset The byte offset from the beginning of the file.
    */
    void
    seek(std::uint64_t offset)
    {
        system::error_code ec;
        impl_.seek(offset, ec);
        if(ec.failed())
            detail::throw_system_error(ec);
    }

    /** Read data from the file.

        @return The number of bytes read. Returns
        0 on end-of-file or if an error occurs (in
        which case @p ec is set).

        @param buffer The buffer to store the read data.

        @param n The number of bytes to read.

        @param ec Set to the error, if any occurred.
    */
    std::size_t
    read(void* buffer, std::size_t n, system::error_code& ec)
    {
        return impl_.read(buffer, n, ec);
    }

    /** Read data from the file.

        @par Exception Safety
        Exception thrown if operation fails.

        @throw system_error
        Operation fails.

        @return The number of bytes read. Returns
        0 on end-of-file.

        @param buffer The buffer to store the read data.

        @param n The number of bytes to read.
    */
    std::size_t
    read(void* buffer, std::size_t n)
    {
        system::error_code ec;
        auto r = impl_.read(buffer, n, ec);
        if(ec.failed())
            detail::throw_system_error(ec);
        return r;
    }

    /** Write data to the file.

        @return The number of bytes written.
        Returns 0 on error (in which case @p ec is
        set).

        @param buffer The buffer containing the data to write.

        @param n The number of bytes to write.

        @param ec Set to the error, if any occurred.
    */
    std::size_t
    write(void const* buffer, std::size_t n, system::error_code& ec)
    {
        return impl_.write(buffer, n, ec);
    }

    /** Write data to the file.

        @par Exception Safety
        Exception thrown if operation fails.

        @throw system_error
        Operation fails.

        @return The number of bytes written.

        @param buffer The buffer containing the data to write.

        @param n The number of bytes to write.
    */
    std::size_t
    write(void const* buffer, std::size_t n)
    {
        system::error_code ec;
        auto r = impl_.write(buffer, n, ec);
        if(ec.failed())
            detail::throw_system_error(ec);
        return r;
    }
};

} // capy
} // boost

#endif
