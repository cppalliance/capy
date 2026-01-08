Here's the compressed context:

---

## UTF-8 Everywhere: C++ Library Implementor's Guide

### Core Principle
Use UTF-8 internally everywhere. Convert to UTF-16 only at Windows API boundaries. Never store wide strings.

### Why UTF-8

- Variable-length like UTF-16, but denser for mixed content (ASCII markup + any language)
- Endianness independent
- Sorts lexicographically same as UTF-32
- No BOM needed or recommended
- ASCII bytes never appear in multi-byte sequences (safe to search for `/`, `<`, `'` naively)
- `std::exception::what()` and `localeconv` only work with narrow strings

### Why Not UTF-16

- Variable-length (surrogate pairs) despite common misconception
- `wchar_t` is 2 bytes on Windows, 4 bytes elsewhere
- Wastes space on ASCII-heavy content (paths, XML, JSON, HTTP)
- Byte order issues (LE vs BE)

### Windows Realities

- Narrow Win32 APIs use ANSI codepage, not UTF-8—broken by design
- Must use wide APIs (`CreateFileW`, `_wfopen`) for Unicode support
- MSVC `fstream` doesn't accept UTF-8 filenames; use non-standard wide overload
- `std::filesystem::path` stores UTF-16 internally on Windows; `string()` converts using locale (wrong)

### Implementation Rules

1. **Internal storage**: Always `std::string` / `char*` containing UTF-8
2. **Windows API calls**: Convert at the call site
   ```cpp
   CreateFileW(widen(utf8_path).c_str(), ...)
   std::ifstream ifs(widen(utf8_path));  // MSVC extension
   ```
3. **Never use**: `wchar_t` for storage, `TCHAR`, `LPTSTR`, `_T()` macros
4. **Always define**: `UNICODE` and `_UNICODE` (catches accidental ANSI API use)
5. **Conversion functions**:
   ```cpp
   std::wstring widen(std::string_view utf8);   // UTF-8 → UTF-16
   std::string narrow(std::wstring_view utf16); // UTF-16 → UTF-8
   ```

### Conversion Cost

Measured overhead of UTF-8→UTF-16 conversion when opening files: **~1%** (I/O dominates).

### What "Character" Means

| Term | Definition |
|------|------------|
| Code unit | Single byte (UTF-8), single `wchar_t` (UTF-16) |
| Code point | Unicode scalar value (1-4 code units in UTF-8) |
| Grapheme cluster | User-perceived character (may span multiple code points) |

**Key insight**: Code point count is rarely useful. Most operations work on bytes (code units) or grapheme clusters. UTF-8 byte length is the practical measure for storage/protocol limits.

### UTF-8 Search Property

UTF-8's self-synchronizing design means:
- ASCII characters never appear as part of multi-byte sequences
- Can search for `/`, `.`, `<` with naive byte comparison
- Can search for UTF-8 substrings without respecting code point boundaries

### Validation

Valid UTF-8 requires:
- Correct continuation byte patterns (10xxxxxx)
- No overlong encodings
- No surrogate code points (U+D800–U+DFFF)
- No values beyond U+10FFFF

### File I/O

- Always write UTF-8 to files
- Use binary mode (`std::ios::binary`) for consistent line endings (`\n`)
- On Windows, use wide path overloads for `fstream`/`fopen`

---

This is everything a C++ path/string library implementor needs from the manifesto.