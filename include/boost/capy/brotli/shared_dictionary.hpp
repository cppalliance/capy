//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_BROTLI_SHARED_DICTIONARY_HPP
#define BOOST_CAPY_BROTLI_SHARED_DICTIONARY_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/brotli/types.hpp>
#include <boost/capy/core/polystore_fwd.hpp>

namespace boost {
namespace capy {
namespace brotli {

/** Opaque structure that holds shared dictionary data. */
struct shared_dictionary;

/** Shared dictionary data format.

    These values specify the format of dictionary data
    being attached to an encoder or decoder.
*/
enum class shared_dictionary_type
{
    /** Raw dictionary data. */
    raw = 0,

    /** Serialized dictionary format. */
    serialized = 1
};

/** Provides the Brotli shared dictionary API.

    This service interface exposes Brotli shared dictionary
    functionality through a set of virtual functions.
*/
struct BOOST_SYMBOL_VISIBLE
    shared_dictionary_service
{
#if 0
    virtual shared_dictionary*
    create_instance(
        alloc_func alloc_func,
        free_func free_func,
        void* opaque) const noexcept = 0;

    virtual void
    destroy_instance(
        shared_dictionary* dict) const noexcept = 0;

    virtual bool
    attach(
        shared_dictionary* dict,
        shared_dictionary_type type,
        std::size_t data_size,
        const uint8_t data[]) const noexcept = 0;
#endif
};

/** Install the shared dictionary service into a polystore.
    @param ctx The polystore to install the service into.
    @return A reference to the installed shared dictionary service.
*/
BOOST_CAPY_DECL
shared_dictionary_service&
install_shared_dictionary_service(polystore& ctx);

} // brotli
} // capy
} // boost

#endif
