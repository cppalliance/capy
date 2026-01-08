`std::filesystem::path` has several well-known interface friction points:

**Encoding ambiguity**
- Constructor accepts `const char*` but interpretation is platform-dependent (UTF-8 on POSIX, ACP on Windows)
- `string()` returns native encoding, `u8string()` returns UTF-8, but pre-C++20 `u8string()` returned `std::string`, post-C++20 returns `std::u8string`—a breaking change
- No way to construct from UTF-8 `std::string` portably without `u8path()` (now deprecated)

**Implicit conversions**
- Implicit conversion to `std::wstring` on Windows causes silent ODR violations and ABI issues across DLL boundaries
- `c_str()` returns `const value_type*` (platform-dependent)—easy to confuse with `string().c_str()` which has different semantics

**Iterator semantics**
- Iterates over *components*, not characters—surprising to users expecting string-like behavior
- `path("/foo/bar").begin()` yields `"/"`, `"foo"`, `"bar"`—not intuitive for manipulation

**Append vs concatenate**
- `operator/=` and `append()` add separators
- `operator+=` and `concat()` don't
- Easy to confuse, hard to remember which is which

**Performance**
- Typically heap-allocates (no SBO guarantee)
- Copies on most operations rather than views
- No `string_view`-like equivalent in the standard

**Normalization**
- `lexically_normal()` exists but no in-place normalization
- Comparison doesn't normalize first—`"foo//bar"` ≠ `"foo/bar"`

For library work, many projects define a thin wrapper or use `std::string` with explicit encoding conventions to avoid these issues.
