//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ZLIB_COMPRESSION_LEVEL_HPP
#define BOOST_CAPY_ZLIB_COMPRESSION_LEVEL_HPP

namespace boost {
namespace capy {
namespace zlib {

/** Compression level constants.

    These values control the trade-off between compression
    speed and compression ratio. Higher values produce better
    compression but take more time. Level 0 disables compression
    entirely, storing data uncompressed. Levels 1-9 provide
    increasing compression, with 6 being the default compromise
    between speed and ratio.

    @code
    boost::capy::zlib::stream st = {};
    auto& deflate_svc = boost::capy::zlib::install_deflate_service(ctx);

    // Use best speed for time-critical operations
    deflate_svc.init(st, boost::capy::zlib::best_speed);

    // Use best compression for archival storage
    deflate_svc.init(st, boost::capy::zlib::best_compression);

    // Use default compression for balanced performance
    deflate_svc.init(st, boost::capy::zlib::default_compression);
    @endcode
*/
enum compression_level
{
    /** Use the default compression level. */
    default_compression = -1,

    /** No compression is performed. */
    no_compression      = 0,

    /** Fastest compression speed with minimal compression. */
    best_speed          = 1,

    /** Best compression ratio with slower speed. */
    best_compression    = 9
};

} // zlib
} // capy
} // boost

#endif
