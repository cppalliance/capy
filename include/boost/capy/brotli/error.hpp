//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BROTLI_ERROR_HPP
#define BOOST_CAPY_BROTLI_ERROR_HPP

#include <boost/capy/detail/config.hpp>

namespace boost {
namespace capy {
namespace brotli {

/** Error codes returned from decode functions.

    Negative values are errors, positive values are used
    for special but normal events.
*/
enum class error
{
    no_error = 0,

    /* Same as decoder_result values */
    success           = 1,
    needs_more_input  = 2,
    needs_more_output = 3,

    /* Errors caused by invalid input */
    format_exuberant_nibble        = -1,
    format_reserved                = -2,
    format_exuberant_meta_nibbl    = -3,
    format_simple_huffman_alphabet = -4,
    format_simple_huffman_same     = -5,
    format_cl_space                = -6,
    format_huffman_space           = -7,
    format_context_map_repea       = -8,
    format_block_length_1          = -9,
    format_block_length_2          = -10,
    format_transform               = -11,
    format_dictionary              = -12,
    format_window_bits             = -13,
    format_padding_1               = -14,
    format_padding_2               = -15,
    format_distance                = -16,

    /* -17 code is reserved */

    compound_dictionary = -18,
    dictionary_not_set  = -19,
    invalid_arguments   = -20,

    /* Memory allocation problems */
    alloc_context_modes = -21,
    /* Literal, insert and distance trees together */
    alloc_tree_groups = -22,
    /* -23..-24 codes are reserved for distinct tree groups */
    alloc_context_map   = -25,
    alloc_ring_buffer_1 = -26,
    alloc_ring_buffer_2 = -27,
    /* -28..-29 codes are reserved for dynamic ring-buffer allocation */
    alloc_block_type_trees = -30,

    /* "Impossible" states */
    unreachable = -31
};

} // brotli
} // capy
} // boost

#include <boost/capy/brotli/impl/error.hpp>

#endif
