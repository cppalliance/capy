//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DATASTORE_HPP
#define BOOST_CAPY_DATASTORE_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/polystore.hpp>

namespace boost {
namespace capy {

/** A polymorphic data container with clear functionality.

    This class extends @ref polystore to provide a container
    for type-erased objects with an explicit clear operation.
*/
class datastore : public polystore
{
public:
    /** Constructor

        Constructs an empty datastore.
    */
    datastore() = default;

    /** Remove and destroy all stored objects.

        All stored objects are destroyed in the reverse order
        of construction. The container is left empty.
    */
    void clear() noexcept
    {
        polystore::clear();
    }
};

} // capy
} // boost

#endif
