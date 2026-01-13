Look in context/ for information

# Cross-Platform Compilation

All code MUST compile and pass tests on ALL configurations in `.github/workflows/ci.yml`. This includes:

- Multiple compilers: MSVC, clang-cl, MinGW, Apple-Clang, GCC 11-15, Clang 14-20
- Multiple platforms: Windows, macOS, Linux
- Both 32-bit and 64-bit architectures
- C++20, C++23, and C++2c standards
- ASan and UBSan sanitizer builds
- Warnings treated as errors

Key portability rules:
- Use `std::size_t` for sizes, `std::uintptr_t` for pointer-to-integer casts
- Do not assume heap deallocation order matches allocation order
- Prefer standard C++ over compiler extensions
- Include what you use; don't rely on transitive includes
