//
// Copyright (c) 2022 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_FILE_MODE_HPP
#define BOOST_CAPY_FILE_MODE_HPP

#include <boost/capy/detail/config.hpp>

namespace boost {
namespace capy {

/** File open modes

    These modes are used when opening files using
    instances of the @ref file.

    @code
    file_mode           access          sharing     seeking     file              std mode
    --------------------------------------------------------------------------------------
    read                read-only       shared      random      must exist          "rb"
    scan                read-only       shared      sequential  must exist          "rbS"
    write               read/write      exclusive   random      create/truncate     "wb+"
    write_new           read/write      exclusive   random      must not exist      "wbx"
    write_existing      read/write      exclusive   random      must exist          "rb+"
    append              write-only      exclusive   sequential  create/truncate     "ab"
    append_existing     write-only      exclusive   sequential  must exist          "ab"
    @endcode

    @see
        @ref file.
*/
enum class file_mode
{
    /** Random read-only access to an existing file.
    */
    read,

    /** Sequential read-only access to an existing file.
    */
    scan,

    /** Random reading and writing to a new or truncated file.

        This mode permits random-access reading and writing
        for the specified file. If the file does not exist
        prior to the function call, it is created with an
        initial size of zero bytes. Otherwise if the file
        already exists, the size is truncated to zero bytes.
    */
    write,

    /** Random reading and writing to a new file only.

        This mode permits random-access reading and writing
        for the specified file. The file will be created with
        an initial size of zero bytes. If the file already exists
        prior to the function call, an error is returned and
        no file is opened.
    */
    write_new,

    /** Random write-only access to existing file.

        If the file does not exist, an error is generated.
    */
    write_existing,

    /** Appending to a new or truncated file.

        The current file position shall be set to the end of
        the file prior to each write.

        @li If the file does not exist, it is created.

        @li If the file exists, it is truncated to
        zero size upon opening.
    */
    append,

    /** Appending to an existing file.

        The current file position shall be set to the end of
        the file prior to each write.

        If the file does not exist, an error is generated.
    */
    append_existing
};

} // capy
} // boost

#endif
