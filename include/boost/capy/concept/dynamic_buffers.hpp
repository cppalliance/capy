//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_DYNAMIC_BUFFERS_HPP
#define BOOST_CAPY_CONCEPT_DYNAMIC_BUFFERS_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>

#include <cstddef>
#include <type_traits>

namespace boost {
namespace capy {

/** Concept for resizable buffer types with prepare/commit semantics.

    Types satisfying this concept provide a two-phase write model:
    call `prepare(n)` to get writable space, write data, then call
    `commit(n)` to make those bytes readable via `data()`.

    @par Semantic Requirements
    - `data()` returns buffer sequence valid until next mutating operation
    - `prepare(n)` returns buffer sequence valid until `commit()` or next `prepare()`
    - Types may reference external storage; caller manages lifetime

    @par Example
    @code
    flat_buffers fb( storage, sizeof( storage ) );
    auto mb = fb.prepare( 100 );    // get writable region
    std::size_t n = read( sock, mb );
    fb.commit( n );                 // make n bytes readable
    process( fb.data() );           // access committed data
    fb.consume( fb.size() );        // discard processed data
    @endcode

    @see DynamicBuffersParam
*/
template<class T>
concept DynamicBuffers =
    requires(T& t, T const& ct, std::size_t n)
    {
        typename T::const_buffers_type;
        typename T::mutable_buffers_type;
        { ct.size() } -> std::convertible_to<std::size_t>;
        { ct.max_size() } -> std::convertible_to<std::size_t>;
        { ct.capacity() } -> std::convertible_to<std::size_t>;
        { ct.data() } -> std::same_as<typename T::const_buffers_type>;
        { t.prepare(n) } -> std::same_as<typename T::mutable_buffers_type>;
        t.commit(n);
        t.consume(n);
    } &&
    ConstBufferSequence<typename T::const_buffers_type> &&
    MutableBufferSequence<typename T::mutable_buffers_type>;

/** Concept for valid DynamicBuffers parameter passing to coroutines.

    This concept constrains how a DynamicBuffers type can be passed
    to coroutine-based I/O functions. It allows:

    - **Lvalues** of any DynamicBuffers (caller manages lifetime)
    - **Rvalues** only for types with `is_dynamic_buffers_adapter` tag

    The distinction exists because some buffer types (like `flat_buffers`)
    store bookkeeping internally that would be lost if passed by rvalue,
    while adapters (like `string_buffers`) update external storage directly.

    @par Adapter Types
    Types safe to pass as rvalues define a nested tag:
    @code
    class string_buffers {
    public:
        using is_dynamic_buffers_adapter = void;
        // ...
    };
    @endcode

    @par Example
    @code
    // OK: lvalue reference
    flat_buffers fb( storage );
    co_await read( stream, fb );

    // OK: adapter as rvalue, string retains data
    std::string s;
    co_await read( stream, string_buffers( s ) );

    // ERROR: non-adapter rvalue
    co_await read( stream, flat_buffers( storage ) );  // compile error
    @endcode

    @see DynamicBuffers
*/
template<class B>
concept DynamicBuffersParam =
    DynamicBuffers<std::remove_cvref_t<B>> &&
    (std::is_lvalue_reference_v<B> ||
     requires { typename std::remove_cvref_t<B>::is_dynamic_buffers_adapter; });

} // capy
} // boost

#endif
