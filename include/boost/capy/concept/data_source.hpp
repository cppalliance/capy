//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_DATA_SOURCE_HPP
#define BOOST_CAPY_CONCEPT_DATA_SOURCE_HPP

#include <boost/capy/buffers/buffer.hpp>

#include <type_traits>

namespace boost {
namespace capy {
namespace buffers {

/** Concept for types that model DataSource.

    A data source presents a binary object as a constant buffer sequence.

    @par Requirements
    @code
    struct DataSource
    {
        DataSource( DataSource&& ) noexcept;
        ConstBufferSequence data() const noexcept;
    };
    @endcode

    Where `const_buffer_sequence<ConstBufferSequence>` is satisfied.
*/
template<class T>
concept data_source =
    std::is_nothrow_move_constructible_v<T> &&
    requires(T const& t)
    {
        { t.data() } -> const_buffer_sequence;
    };

} // buffers
} // capy
} // boost

#endif
