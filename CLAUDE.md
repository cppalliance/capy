# CLAUDE.md v2

## Code Style

- C++11 unless otherwise specified
- Boost C++ Libraries naming conventions (snake_case)
- 4-space indentation, no tabs
- Braces on their own line for classes/functions
- "detail" namespace symbols are never public
- public headers in "include/"
- library cpp and private header files in "src/"
- test files in "test/"
- Prefer RAII rollback guards over try-catch for cleanup

## Javadoc Documentation

Follow Boost C++ Libraries Javadoc style:

- Brief descriptions on first line after `/**`
- Functions returning values: brief starts with "Return"
- Use `@param` for function parameters
- Use `@tparam` for template parameters, except:
  - Variadic args (`Args...`) — omit
  - Types deduced from function parameters — omit (self-evident from `@param`)
- Use `@return` for return value details
- Use `@pre` for preconditions
- Use `@post` for postconditions
- Use `@throws` for exceptions
- Use `@note` for important notes
- Use `@see` for cross-references
- Use `@code` / `@endcode` for examples

### Examples

```cpp
/** Return the size of the buffer sequence.

    @param buffers The buffer sequence to measure.

    @return The total byte count.
*/
template<class BufferSequence>
std::size_t
buffer_size(BufferSequence const& buffers);
```

No `@tparam` needed—`BufferSequence` is evident from `@param buffers`.

```cpp
/** Return the default value.

    @tparam T The value type.
*/
template<class T>
T default_value();
```

`@tparam` needed—`T` has no corresponding function parameter.

## Visibility and Linkage

### Platform behavior
- MSVC class-level dllexport: exports vtable, typeinfo, ALL members (including private)
- GCC/Clang visibility("default") on class: exports vtable and typeinfo only
- MinGW: GCC frontend (key function rule) + Windows ABI (needs dllexport)

### vtable requirements
- Construction/destruction requires vtable (vptr initialization)
- dynamic_cast/typeid require typeinfo
- Polymorphic class crossing DLL boundary: MUST export vtable

### Strategy by class type
- Non-polymorphic: per-member export (minimizes symbols)
- Polymorphic, DLL boundary: class-level export (MSVC has no alternative)
- Polymorphic, header-only: BOOST_SYMBOL_VISIBLE (typeinfo consistency, not export)

### Pitfalls
- Class dllexport + private member with incomplete type = link error
- Missing key function export on GCC = "undefined reference to vtable"
- Implicit special members not exported unless class-level export used

## Preferences

- Concise, dry answers
- Full files, not diffs
- Accurate, compiling C++ code
# CLAUDE.md

## Code Style

- C++11 unless otherwise specified
- Boost C++ Libraries naming conventions (snake_case)
- 4-space indentation, no tabs
- Braces on their own line for classes/functions
- Symbols in "detail" namespaces are never public
- public headers in "include/"
- library cpp files in "src/"

## Javadoc Documentation

Follow Boost C++ Libraries Javadoc style:

- Brief descriptions on first line after `/**`
- Functions returning values: brief starts with "Return"
- Use `@param` for function parameters
- Use `@tparam` for template parameters, except:
  - Variadic args (`Args...`) — omit
  - Types deduced from function parameters — omit (self-evident from `@param`)
- Use `@return` for return value details
- Use `@pre` for preconditions
- Use `@post` for postconditions
- Use `@throws` for exceptions
- Use `@note` for important notes
- Use `@see` for cross-references
- Use `@code` / `@endcode` for examples

## Examples

```cpp
/** Return the size of the buffer sequence.

    @param buffers The buffer sequence to measure.

    @return The total byte count.
*/
template<class BufferSequence>
std::size_t
buffer_size(BufferSequence const& buffers);
```

No `@tparam` needed—`BufferSequence` is evident from `@param buffers`.

```cpp
/** Return the default value.

    @tparam T The value type.
*/
template<class T>
T default_value();
```

`@tparam` needed—`T` has no corresponding function parameter.

## Preferences

- Concise, dry answers
- Full files, not diffs
- Accurate, compiling C++ code
