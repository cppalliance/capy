# API Evaluation: capy::path vs std::filesystem::path

## Summary

| Aspect | capy::path | std::filesystem::path |
|--------|-----------|----------------------|
| Internal encoding | Always UTF-8 | Platform-dependent (UTF-16 Windows, UTF-8 POSIX) |
| Separator | Always forward slash | Platform-dependent |
| Validation | At construction | None (any string accepted) |
| Invariant | Always holds a valid path | None |
| UTF-8 access | `string()` always returns UTF-8 | No portable method |
| Decomposition | Returns views (zero allocation) | Allocates new path objects |

---

## 1. The Core Problem: No Portable UTF-8 Access

`std::filesystem::path` provides no portable way to get UTF-8 bytes:

```cpp
std::filesystem::path p("C:/Users/用户/file.txt");

p.string();      // Platform-dependent encoding
p.u8string();    // Returns std::u8string, not std::string
```

With `capy::path`:

```cpp
capy::path p("C:/Users/用户/file.txt");

p.string();      // Always UTF-8, always std::string
```

This single difference eliminates an entire class of cross-platform bugs.

---

## 2. Construction Consistency

`std::filesystem::path` accepts any string and interprets it according to platform rules:

```cpp
std::string s = get_from_config_file();
std::filesystem::path p(s);  // Interpretation is platform-dependent
```

`capy::path` always interprets input as UTF-8 and validates:

```cpp
std::string s = get_from_config_file();
capy::path p(s);  // Always UTF-8, throws if invalid

// Or non-throwing:
auto result = try_parse_path(s);
if (!result) {
    log_error(result.error());  // Detailed error: invalid_utf8, illegal_character, etc.
}
```

---

## 3. The Invariant

**`capy::path` always holds a valid path.** Once constructed, you know:

- The string is well-formed UTF-8
- No embedded NUL characters
- No illegal filename characters (`<>"|?*`)
- No trailing dots or spaces in components

Invalid input is rejected at construction, not when you try to use the path.

```cpp
capy::path p("foo<bar");  // Throws immediately

// vs std::filesystem::path
std::filesystem::path p("foo<bar");  // Accepted
std::ofstream ofs(p);                 // Fails here, far from the bug
```

---

## 4. Decomposition Without Allocation

`std::filesystem::path` decomposition functions allocate new path objects:

```cpp
std::filesystem::path p("/home/user/documents/file.txt");

auto parent = p.parent_path();   // Allocates
auto fname = p.filename();       // Allocates
auto stem = p.stem();            // Allocates
auto ext = p.extension();        // Allocates
```

`capy::path` returns views into the existing storage:

```cpp
capy::path p("/home/user/documents/file.txt");

path_view parent = p.parent_path();   // No allocation
path_view fname = p.filename();       // No allocation
path_view stem = p.stem();            // No allocation
path_view ext = p.extension();        // No allocation
```

---

## 5. Conversion Cost Summary

| Operation | capy::path | std::filesystem::path |
|-----------|-----------|----------------------|
| Construction | O(n) validation | O(1) no validation |
| `string()` | O(1) always UTF-8 | Platform-dependent encoding |
| `filename()`, `parent_path()`, etc. | O(1) returns view | O(n) allocates |
| Iteration | Zero allocation | Allocates per element |
| Serialization | Zero cost (already UTF-8) | May need encoding conversion |

**Key insight:** `capy::path` moves the cost to construction time (where it can be validated once) rather than spreading it across every operation.

---

## When to Use Each

**Use `capy::path` when:**
- Building cross-platform applications
- Serializing paths (config files, databases, network)
- You want early validation and clear error handling

**Use `std::filesystem::path` when:**
- Platform-specific code that never serializes paths
- Interfacing with APIs that return `std::filesystem::path`

**Interop:**
```cpp
// capy::path to std::filesystem::path
std::filesystem::path fsp = p.filesystem_path();

// std::filesystem::path to capy::path
capy::path p(fsp.generic_u8string());
```
