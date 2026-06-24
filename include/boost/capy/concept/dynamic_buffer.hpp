//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_CONCEPT_DYNAMIC_BUFFER_HPP
#define BOOST_CAPY_CONCEPT_DYNAMIC_BUFFER_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/buffers.hpp>

#include <cstddef>
#include <type_traits>

/*
    LLM INSTRUCTION: Passing Dynamic Buffers Correctly

    There are two kinds of dynamic buffer types:

    1. VALUE TYPES (a user-defined buffer that owns its storage,
       e.g. one holding a std::array member)
       - Store the bytes and bookkeeping inside the object itself
       - MUST be passed by lvalue reference to preserve state
       - Passing as rvalue loses the data on coroutine suspend

    2. WRAPPER ADAPTERS (all of Capy's provided types:
       flat_dynamic_buffer, circular_dynamic_buffer,
       vector_dynamic_buffer, string_dynamic_buffer)
       - Reference external storage (a caller-owned array,
         std::string, or std::vector)
       - Safe to pass as rvalues; the external object retains data
       - Define `using is_dynamic_buffer_adapter = void;`

    When writing functions:

    - NON-COROUTINE: Use `DynamicBuffer auto&` (lvalue ref)
        void fill(DynamicBuffer auto& buffers);

    - COROUTINE: Use `DynamicBufferParam auto&&` (forwarding ref)
        io_task<size_t> read(DynamicBufferParam auto&& buffers);

    DynamicBufferParam enforces safe passing at compile time:
    accepts lvalues of any DynamicBuffer, but rvalues only for adapters.

    WRONG patterns that compile but misbehave:
    - Using DynamicBuffer in coroutines (allows dangerous rvalues)
    - Using lvalue ref with DynamicBufferParam (rejects valid adapters)
*/

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

    @par Value Types vs Wrapper Adapters
    Dynamic buffer types fall into two categories:

    - **Value types** store the bytes and bookkeeping inside the object.
      Passing one as an rvalue to a coroutine loses state on suspend. Capy
      ships no value types; this category is for user-defined buffers that
      own their storage internally.

    - **Wrapper adapters** reference external storage and are safe as rvalues
      since the external object persists. All of Capy's provided dynamic
      buffers are adapters: `flat_dynamic_buffer` and `circular_dynamic_buffer`
      (over a caller-owned array), `vector_dynamic_buffer`, and
      `string_dynamic_buffer`. Each defines `using is_dynamic_buffer_adapter = void;`.

    @par Conforming Signatures
    For **non-coroutine** functions, use `DynamicBuffer auto&`:
    @code
    void fill( DynamicBuffer auto& buffers );
    @endcode

    For **coroutine** functions, use `DynamicBufferParam auto&&` instead.
    This concept enforces lifetime safety: it accepts lvalues of any
    DynamicBuffer, but restricts rvalues to adapter types only. Using
    plain `DynamicBuffer` in coroutines allows dangerous rvalue passing
    that compiles but silently loses data on suspend.
    @code
    io_task<std::size_t>
    read( ReadSource auto& src, DynamicBufferParam auto&& buffers );
    @endcode

    @par Example
    @code
    flat_dynamic_buffer fb( storage, sizeof( storage ) );
    auto mb = fb.prepare( 100 );    // get writable region
    std::size_t n = read( sock, mb );
    fb.commit( n );                 // make n bytes readable
    process( fb.data() );           // access committed data
    fb.consume( fb.size() );        // discard processed data
    @endcode

    @see DynamicBufferParam
*/
template<class T>
concept DynamicBuffer =
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

/** Concept for valid DynamicBuffer parameter passing to coroutines.

    This concept constrains how a DynamicBuffer type can be passed
    to coroutine-based I/O functions. It allows:

    - **Lvalues** of any DynamicBuffer (caller manages lifetime)
    - **Rvalues** only for types with `is_dynamic_buffer_adapter` tag

    The distinction exists because a value type stores its bytes and
    bookkeeping internally, which would be lost if passed by rvalue, while
    adapters (such as Capy's `flat_dynamic_buffer` or `string_dynamic_buffer`)
    update external storage directly.

    @par Conforming Signatures
    For coroutine functions, use a forwarding reference:
    @code
    io_task<std::size_t>
    read( ReadSource auto& source, DynamicBufferParam auto&& buffers );
    @endcode

    The forwarding reference is essential because the concept inspects
    the value category to enforce the lvalue/rvalue rules. Using the
    wrong reference type causes incorrect behavior:
    @code
    // owning_buffer is a user-defined value type: it owns its storage and
    // has no is_dynamic_buffer_adapter tag. ob is an lvalue of it.
    owning_buffer ob;

    // WRONG: lvalue ref rejects valid rvalue adapters
    void bad1( DynamicBufferParam auto& buffers );
    bad1( ob );                            // OK
    bad1( string_dynamic_buffer( &s ) );   // compile error, but should work

    // WRONG: const ref deduces non-reference, rejects non-adapters
    void bad2( DynamicBufferParam auto const& buffers );
    bad2( ob );                            // compile error, but should work
    bad2( string_dynamic_buffer( &s ) );   // OK (adapter only)

    // CORRECT: forwarding ref enables proper checking
    void good( DynamicBufferParam auto&& buffers );
    good( ob );                            // OK: lvalue value type
    good( string_dynamic_buffer( &s ) );   // OK: adapter rvalue
    good( owning_buffer{} );               // compile error: non-adapter rvalue
    @endcode

    @par Adapter Types
    Types safe to pass as rvalues define a nested tag:
    @code
    class string_dynamic_buffer {
    public:
        using is_dynamic_buffer_adapter = void;
        // ...
    };
    @endcode

    @par Example
    @code
    // OK: lvalue reference (any DynamicBuffer)
    char storage[1024];
    flat_dynamic_buffer fb( storage, sizeof( storage ) );
    co_await read( stream, fb );

    // OK: adapter as rvalue — flat references the caller's array,
    // which persists across the suspend
    co_await read( stream, flat_dynamic_buffer( storage, sizeof( storage ) ) );

    // OK: adapter as rvalue — the string retains the data
    std::string s;
    co_await read( stream, string_dynamic_buffer( &s ) );

    // ERROR: a user-defined value type as rvalue loses state on suspend
    co_await read( stream, owning_buffer{} );  // compile error: non-adapter rvalue
    @endcode

    @see DynamicBuffer
*/
template<class B>
concept DynamicBufferParam =
    DynamicBuffer<std::remove_cvref_t<B>> &&
    (std::is_lvalue_reference_v<B> ||
     requires { typename std::remove_cvref_t<B>::is_dynamic_buffer_adapter; });

} // capy
} // boost

#endif
