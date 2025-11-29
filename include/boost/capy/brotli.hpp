//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

/** @file
    Brotli compression and decompression library.

    This header includes all Brotli-related functionality including
    encoding, decoding, error handling, and shared dictionary support.

    Brotli is a generic-purpose lossless compression algorithm that compresses
    data using a combination of a modern variant of the LZ77 algorithm, Huffman
    coding and 2nd order context modeling, with a compression ratio comparable
    to the best currently available general-purpose compression methods.

    @code
    #include <boost/capy/brotli.hpp>
    #include <boost/capy/datastore.hpp>

    // Create a datastore for services
    boost::capy::datastore ctx;

    // Install compression and decompression services
    auto& encoder = boost::capy::brotli::install_encode_service(ctx);
    auto& decoder = boost::capy::brotli::install_decode_service(ctx);
    @endcode
*/

#ifndef BOOST_CAPY_BROTLI_HPP
#define BOOST_CAPY_BROTLI_HPP

#include <boost/capy/brotli/decode.hpp>
#include <boost/capy/brotli/encode.hpp>
#include <boost/capy/brotli/error.hpp>
#include <boost/capy/brotli/shared_dictionary.hpp>
#include <boost/capy/brotli/types.hpp>

#endif
