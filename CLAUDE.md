# CLAUDE.md

## Code Style

- C++11 unless otherwise specified
- Boost C++ Libraries naming conventions (snake_case)
- 4-space indentation, no tabs
- Braces on their own line for classes/functions

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
