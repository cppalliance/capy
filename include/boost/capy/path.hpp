//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt
//

/*******************************************************************************

                        IMPLEMENTATION NOTES

    This header declares `path` and `path_view` classes that follow the
    "UTF-8 Everywhere" manifesto (https://utf8everywhere.org/). The design
    solves the interface friction present in std::filesystem::path.

================================================================================
DESIGN PHILOSOPHY
================================================================================

    1. UTF-8 Everywhere
       - All strings are UTF-8 encoded, using `char` and `std::string`
       - No wchar_t, char8_t, char16_t, or char32_t in the public interface
       - Conversion to native wide strings (Windows) happens at API boundaries

    2. Universal Path Format
       - Internal storage always uses forward slashes as separators
       - "C:\foo\bar" is stored as "C:/foo/bar"
       - This enables cross-platform serialization to files, JSON, databases
       - Windows APIs accept forward slashes for most operations anyway

    3. Simple Invariant
       - ALL path and path_view objects hold syntactically valid paths
       - Validation happens at construction time
       - Once constructed, the path is guaranteed valid
       - Empty string is a valid path (consistent with std::filesystem::path)

    4. Two Error Handling Strategies
       - Constructors throw system_error on invalid input
       - Free functions return system::result<T> for error-code style

================================================================================
VALIDITY RULES
================================================================================

    A path is valid if and only if:

    1. UTF-8 Well-Formed
       - No invalid UTF-8 sequences
       - No overlong encodings
       - No surrogate code points (U+D800 to U+DFFF)

    2. No Embedded NUL
       - The string must not contain '\0' except as terminator

    3. No Platform-Illegal Characters in Filenames
       - Reject characters illegal on ANY platform (union of restrictions)
       - Windows forbids: < > : " / \ | ? *
       - Forward slash is the separator, not illegal
       - Colon is allowed only in drive letter position (e.g., "C:")
       - These characters in the path component (filename) are rejected:
           * (asterisk)
           ? (question mark)
           < (less than)
           > (greater than)
           | (pipe)
           " (double quote)
       - Control characters (0x00-0x1F) are rejected

    4. No Trailing Dots or Spaces in Components (Windows restriction)
       - "foo." and "foo " are invalid as filename components
       - This prevents paths that silently fail on Windows

    Validation function signature (internal):

        // Returns error_code, empty on success
        system::error_code validate_path(std::string_view s) noexcept;

================================================================================
INTERNAL STORAGE
================================================================================

    class path:
        - Stores: std::string s_
        - The string uses forward slashes exclusively
        - Null-terminated (std::string guarantees this)

    class path_view:
        - Stores: char const* data_, std::size_t size_
        - Points into valid UTF-8 path data
        - NOT necessarily null-terminated

================================================================================
CONSTRUCTION AND PARSING
================================================================================

    path_view Construction:
    -----------------------
    1. path_view() noexcept
       - Default constructs empty path_view (data_=nullptr, size_=0)

    2. path_view(std::string_view s)
       - Validate s
       - If invalid, throw system_error with appropriate error_code
       - If valid, store pointer and size

    3. path_view(char const* s)
       - Equivalent to path_view(std::string_view(s))

    4. path_view(unchecked_t, char const* data, std::size_t size) [private]
       - Used by path class to create views without re-validation
       - Caller guarantees validity

    path Construction:
    ------------------
    1. path() noexcept
       - Default constructs empty path

    2. path(std::string_view s)
       - Validate s
       - If invalid, throw system_error
       - If valid, copy into s_

    3. path(char const* s)
       - Equivalent to path(std::string_view(s))

    4. path(std::string&& s)
       - Validate s
       - If invalid, throw system_error (string not moved)
       - If valid, move into s_

    5. path(path_view pv)
       - Copy pv's data into s_
       - No validation needed (path_view is already valid)

    6. path(unchecked_t, std::string&& s) [private]
       - Move without validation
       - Used internally when validity is already established

    Free Function Factories:
    ------------------------
    system::result<path_view> try_parse_path_view(std::string_view s) noexcept
    system::result<path_view> try_parse_path_view(char const* s) noexcept
        - Validate s
        - Return path_view on success
        - Return error_code on failure

    system::result<path> try_parse_path(std::string_view s) noexcept
    system::result<path> try_parse_path(char const* s) noexcept
    system::result<path> try_parse_path(std::string&& s) noexcept
        - Validate s
        - Return path on success (moving string in rvalue overload)
        - Return error_code on failure

================================================================================
ERROR CODES
================================================================================

    Define a custom error category for path errors:

    enum class path_error
    {
        invalid_utf8 = 1,       // Malformed UTF-8 sequence
        embedded_null,          // NUL character in path
        illegal_character,      // Character not allowed in filenames
        invalid_drive_spec,     // Malformed drive specification
        trailing_dot_or_space,  // Component ends with dot or space
    };

    Implement error_category for path_error to integrate with system::error_code.

================================================================================
NATIVE CONVERSION
================================================================================

    On POSIX:
    ---------
    native_string() returns a copy of the internal string (already native).
    native_size() returns s_.size().
    to_native(span<char>) copies s_ into the buffer.

    On Windows:
    -----------
    native_string() returns a copy with '/' replaced by '\'.
    native_wstring() converts UTF-8 to UTF-16 and '/' to '\'.

    native_size() returns s_.size() (same length, just different chars).
    native_wsize() returns the number of wchar_t needed for UTF-16.
        - This requires scanning the UTF-8 to count code points
        - Non-BMP code points require 2 wchar_t (surrogate pair)

    to_native(span<char>) copies with separator replacement.
    to_native(span<wchar_t>) converts UTF-8 to UTF-16 with separator replacement.

    UTF-8 to UTF-16 Conversion Notes:
    - Use MultiByteToWideChar on Windows or manual conversion
    - Each UTF-8 code point maps to 1 or 2 UTF-16 code units
    - ASCII (including '/') maps 1:1

    Cost Analysis:
    - native_string() on POSIX: O(n) copy, no transformation
    - native_string() on Windows: O(n) copy with in-place transform
    - native_wstring() on Windows: O(n) allocation + UTF-8 to UTF-16 conversion
    - These costs are acceptable because filesystem APIs are I/O bound

================================================================================
PATH DECOMPOSITION
================================================================================

    All decomposition functions return path_view pointing into the original
    path's storage. They do NOT allocate.

    Components of a path (using "C:/foo/bar/baz.txt" as example):

    root_name()      -> "C:"           (drive letter on Windows, empty on POSIX)
    root_directory() -> "/"            (the separator after root_name)
    root_path()      -> "C:/"          (root_name + root_directory)
    relative_path()  -> "foo/bar/baz.txt"
    parent_path()    -> "C:/foo/bar"
    filename()       -> "baz.txt"
    stem()           -> "baz"
    extension()      -> ".txt"         (includes the dot)

    For POSIX path "/foo/bar/baz.txt":
    root_name()      -> ""             (empty)
    root_directory() -> "/"
    root_path()      -> "/"
    relative_path()  -> "foo/bar/baz.txt"
    parent_path()    -> "/foo/bar"
    filename()       -> "baz.txt"
    stem()           -> "baz"
    extension()      -> ".txt"

    Edge cases:
    - filename() of "/" is empty
    - filename() of "/foo/" is empty
    - filename() of "/foo/." is "."
    - filename() of "/foo/.." is ".."
    - extension() of ".gitignore" is "" (filename is all stem)
    - extension() of "archive.tar.gz" is ".gz"
    - parent_path() of "/" is "/"
    - parent_path() of "foo" is ""

    Implementation approach:
    - Parse from right to left for filename/extension
    - Parse from left to right for root_name/root_directory
    - Return path_view using unchecked constructor (substrings of valid
      paths are valid)

================================================================================
PATH MODIFICATION
================================================================================

    append(path_view p) / operator/=(path_view p):
    ----------------------------------------------
    - If p is absolute, replace *this with p
    - If p has root_name different from *this, replace *this with p
    - Otherwise, append separator (if needed) and p
    - The result is always valid (both inputs are valid)

    concat(std::string_view s) / operator+=(std::string_view s):
    ------------------------------------------------------------
    - Append s directly without separator
    - MUST re-validate the result (s is arbitrary string)
    - Throw system_error if result is invalid

    remove_filename():
    ------------------
    - Remove everything after the last separator
    - "/foo/bar" -> "/foo/"
    - "/foo/bar/" -> "/foo/bar/" (already no filename)
    - "bar" -> ""

    replace_filename(path_view replacement):
    ----------------------------------------
    - Equivalent to: remove_filename(); append(replacement);
    - Throw if result invalid (shouldn't happen if replacement is valid)

    replace_extension(path_view replacement):
    -----------------------------------------
    - Remove current extension (if any)
    - If replacement is not empty and doesn't start with '.', add '.'
    - Append replacement
    - Validate and throw if invalid

================================================================================
PATH GENERATION
================================================================================

    lexically_normal():
    -------------------
    - Remove redundant separators ("foo//bar" -> "foo/bar")
    - Remove "." components ("foo/./bar" -> "foo/bar")
    - Remove ".." and preceding component ("foo/bar/../baz" -> "foo/baz")
    - Preserve leading ".." ("../foo" stays "../foo")
    - Preserve root ("/../foo" -> "/foo", not "../foo")
    - Return new path (does not modify *this)

    lexically_relative(path_view base):
    -----------------------------------
    - Return a relative path from base to *this
    - Both paths should be normalized first (internally)
    - If no relative path exists, return empty path
    - Example: "/a/b/c".lexically_relative("/a/d") -> "../b/c"

    lexically_proximate(path_view base):
    ------------------------------------
    - Same as lexically_relative, but return *this if no relative path exists
    - Never returns empty path

================================================================================
COMPONENT ITERATION (path::iterator)
================================================================================

    The iterator yields path_view for each component.

    For "C:/foo/bar":
        *it++ -> "C:"
        *it++ -> "/"
        *it++ -> "foo"
        *it++ -> "bar"
        it == end()

    For "/foo/bar":
        *it++ -> "/"
        *it++ -> "foo"
        *it++ -> "bar"
        it == end()

    For "foo/bar":
        *it++ -> "foo"
        *it++ -> "bar"
        it == end()

    Implementation:
    - Store pointer to path, current position, current component length
    - operator* returns path_view(unchecked_t{}, p_->data() + pos_, len_)
    - operator++ scans forward to find next component
    - operator-- scans backward to find previous component
    - begin() initializes to first component
    - end() has pos_ == p_->size() (past-the-end position)

    Bidirectional iterator requirements must be satisfied.

================================================================================
SEGMENT ITERATION (path::segment_range)
================================================================================

    Similar to component iteration but yields std::string_view instead of
    path_view. This is lighter weight for code that just needs string data.

    The segments are the same as components, just different return type.

    Implementation is identical to path::iterator but operator* returns
    std::string_view instead of path_view.

================================================================================
COMPARISON AND HASHING
================================================================================

    Comparison:
    -----------
    - Lexicographic comparison of the underlying strings
    - Case-sensitive (even on Windows - normalization is separate concern)
    - compare() returns negative/zero/positive int
    - operator== and operator<=> delegate to string comparison

    Hashing:
    --------
    - hash_value() uses std::hash<std::string_view> on the path data
    - Must produce same hash for path and path_view with same content
    - std::hash specializations delegate to hash_value()

    Implementation:
        std::size_t hash_value(path const& p) noexcept {
            return std::hash<std::string_view>{}(p.string_view());
        }
        std::size_t hash_value(path_view p) noexcept {
            return std::hash<std::string_view>{}(p.string_view());
        }

================================================================================
STREAM OPERATORS
================================================================================

    operator<<(ostream&, path const&):
    ----------------------------------
    - Output the path string directly
    - Use quoted format if path contains spaces? (design choice)
    - Simplest: os << p.string();

    operator>>(istream&, path&):
    ----------------------------
    - Read a string from the stream
    - Validate and assign
    - Set failbit if validation fails

================================================================================
NON-MEMBER OPERATORS
================================================================================

    path operator/(path const& lhs, path_view rhs):
        path result(lhs);
        result /= rhs;
        return result;

    path operator/(path&& lhs, path_view rhs):
        lhs /= rhs;
        return std::move(lhs);

================================================================================
THREAD SAFETY
================================================================================

    - path and path_view have value semantics
    - No shared mutable state
    - Safe to use different instances in different threads
    - Concurrent reads of same instance are safe
    - Concurrent read/write of same instance requires external synchronization

================================================================================
EXCEPTION SAFETY
================================================================================

    - Constructors: Strong guarantee (throw or succeed, no side effects)
    - Modifiers: Strong guarantee where possible
    - concat() may throw after modifying if validation fails - consider
      validating first, then modifying (strong guarantee)
    - Decomposition/query functions: noexcept (never throw)

================================================================================
DIFFERENCES FROM std::filesystem::path
================================================================================

    1. No encoding ambiguity
       - Always UTF-8, no platform-dependent interpretation
       - No u8string(), u16string(), u32string() - just string()

    2. No implicit conversions
       - Explicit constructors prevent silent conversions
       - No implicit conversion to native string types

    3. Clear separator handling
       - Always forward slash internally
       - Native conversion is explicit

    4. Validation at construction
       - Invalid paths cannot exist
       - No need to check validity later

    5. path_view for non-owning references
       - Similar relationship as string/string_view
       - Same invariant guarantee

    6. Error handling choice
       - Constructor throws (like std::filesystem::path)
       - Free function returns result (for error code preference)

*******************************************************************************/

#ifndef NET_PATH_HPP
#define NET_PATH_HPP

#include <boost/system/result.hpp>

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>

namespace net {

namespace system = boost::system;

// Forward declarations
class path;
class path_view;

//------------------------------------------------------------------------------

/** A non-owning reference to a valid path string.

    Invariant: The referenced string is a syntactically valid path.
    The path uses forward slashes as separators (universal format).
    The string is UTF-8 encoded.
*/
class path_view
{
    char const* data_ = nullptr;
    std::size_t size_ = 0;

public:
    using value_type        = char;
    using const_iterator    = char const*;
    using iterator          = const_iterator;
    using size_type         = std::size_t;

    /** Default constructor (empty path).
    */
    path_view() noexcept = default;

    /** Construct from string_view, validates and throws on invalid.

        @throws system_error on invalid path
    */
    explicit path_view(std::string_view s);

    /** Construct from null-terminated string, validates and throws on invalid.

        @throws system_error on invalid path
    */
    explicit path_view(char const* s);

    //--------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------

    /** Return a pointer to the path data.
    */
    char const*
    data() const noexcept
    {
        return data_;
    }

    /** Return the size of the path in bytes.
    */
    std::size_t
    size() const noexcept
    {
        return size_;
    }

    /** Return true if the path is empty.
    */
    bool
    empty() const noexcept
    {
        return size_ == 0;
    }

    /** Return an iterator to the beginning.
    */
    const_iterator
    begin() const noexcept
    {
        return data_;
    }

    /** Return an iterator to the end.
    */
    const_iterator
    end() const noexcept
    {
        return data_ + size_;
    }

    //--------------------------------------------
    //
    // String access
    //
    //--------------------------------------------

    /** Return the path as a string_view.
    */
    std::string_view
    string_view() const noexcept
    {
        return { data_, size_ };
    }

    /** Return the path as a string.
    */
    std::string
    string() const
    {
        return { data_, size_ };
    }

    //--------------------------------------------
    //
    // Path decomposition
    //
    //--------------------------------------------

    /** Return the root name (e.g., "C:" on Windows).
    */
    path_view root_name() const noexcept;

    /** Return the root directory (e.g., "/").
    */
    path_view root_directory() const noexcept;

    /** Return the root path (root_name + root_directory).
    */
    path_view root_path() const noexcept;

    /** Return the path relative to the root.
    */
    path_view relative_path() const noexcept;

    /** Return the parent path.
    */
    path_view parent_path() const noexcept;

    /** Return the filename component.
    */
    path_view filename() const noexcept;

    /** Return the stem (filename without extension).
    */
    path_view stem() const noexcept;

    /** Return the extension (including the dot).
    */
    path_view extension() const noexcept;

    //--------------------------------------------
    //
    // Query
    //
    //--------------------------------------------

    /** Return true if the path is absolute.
    */
    bool is_absolute() const noexcept;

    /** Return true if the path is relative.
    */
    bool is_relative() const noexcept;

    /** Return true if the path has a root name.
    */
    bool has_root_name() const noexcept;

    /** Return true if the path has a root directory.
    */
    bool has_root_directory() const noexcept;

    /** Return true if the path has a root path.
    */
    bool has_root_path() const noexcept;

    /** Return true if the path has a relative path.
    */
    bool has_relative_path() const noexcept;

    /** Return true if the path has a parent path.
    */
    bool has_parent_path() const noexcept;

    /** Return true if the path has a filename.
    */
    bool has_filename() const noexcept;

    /** Return true if the path has a stem.
    */
    bool has_stem() const noexcept;

    /** Return true if the path has an extension.
    */
    bool has_extension() const noexcept;

    //--------------------------------------------
    //
    // Comparison
    //
    //--------------------------------------------

    /** Compare this path to another.

        @return Negative if this < other, zero if equal, positive if this > other
    */
    int compare(path_view other) const noexcept;

    /** Return true if two paths are equal.
    */
    friend bool
    operator==(path_view lhs, path_view rhs) noexcept;

    /** Three-way comparison of two paths.
    */
    friend std::strong_ordering
    operator<=>(path_view lhs, path_view rhs) noexcept;

private:
    friend class path;

    // Private unchecked constructor for use by path
    struct unchecked_t {};

    path_view(
        unchecked_t,
        char const* data,
        std::size_t size) noexcept
        : data_(data)
        , size_(size)
    {
    }
};

/** Parse a string as a path_view without throwing.

    @return The path_view on success, or an error_code on failure
*/
system::result<path_view>
try_parse_path_view(std::string_view s) noexcept;

/** Parse a null-terminated string as a path_view without throwing.

    @return The path_view on success, or an error_code on failure
*/
system::result<path_view>
try_parse_path_view(char const* s) noexcept;

//------------------------------------------------------------------------------

/** An owning, mutable path string.

    Invariant: The string is a syntactically valid path.
    The path uses forward slashes as separators (universal format).
    The string is UTF-8 encoded.
*/
class path
{
    std::string s_;

public:
    using value_type        = char;
    using string_type       = std::string;
    using size_type         = std::size_t;

    class iterator;
    using const_iterator    = iterator;

    //--------------------------------------------
    //
    // Construction
    //
    //--------------------------------------------

    /** Default constructor (empty path).
    */
    path() noexcept = default;

    /** Copy constructor.
    */
    path(path const&) = default;

    /** Move constructor.
    */
    path(path&&) noexcept = default;

    /** Copy assignment.
    */
    path& operator=(path const&) = default;

    /** Move assignment.
    */
    path& operator=(path&&) noexcept = default;

    /** Construct from string_view, validates and throws on invalid.

        @throws system_error on invalid path
    */
    explicit path(std::string_view s);

    /** Construct from null-terminated string, validates and throws on invalid.

        @throws system_error on invalid path
    */
    explicit path(char const* s);

    /** Construct from string (moves if valid), validates and throws on invalid.

        @throws system_error on invalid path
    */
    explicit path(std::string&& s);

    /** Construct from path_view.
    */
    path(path_view pv);

    //--------------------------------------------
    //
    // Conversion
    //
    //--------------------------------------------

    /** Convert to path_view.
    */
    operator path_view() const noexcept;

    /** Return a path_view of this path.
    */
    path_view
    view() const noexcept
    {
        return path_view(
            path_view::unchecked_t{},
            s_.data(),
            s_.size());
    }

    //--------------------------------------------
    //
    // String access (UTF-8, universal separators)
    //
    //--------------------------------------------

    /** Return a null-terminated string.
    */
    char const*
    c_str() const noexcept
    {
        return s_.c_str();
    }

    /** Return a reference to the underlying string.
    */
    std::string const&
    string() const noexcept
    {
        return s_;
    }

    /** Return a string_view of the path.
    */
    std::string_view
    string_view() const noexcept
    {
        return s_;
    }

    //--------------------------------------------
    //
    // Native format conversion
    //
    //--------------------------------------------

    /** Return the path in native format.

        On POSIX, this returns a copy of the internal string.
        On Windows, this converts forward slashes to backslashes.
    */
    std::string native_string() const;

#ifdef _WIN32
    /** Return the path as a wide string in native format.

        Converts UTF-8 to UTF-16 and forward slashes to backslashes.
    */
    std::wstring native_wstring() const;
#endif

    /** Return the size needed for native_string().
    */
    std::size_t
    native_size() const noexcept
    {
        return s_.size();
    }

    /** Convert to native format into a caller-provided buffer.

        @param out The output buffer
    */
    void to_native(std::span<char> out) const;

#ifdef _WIN32
    /** Return the size needed for native_wstring().
    */
    std::size_t native_wsize() const noexcept;

    /** Convert to native wide format into a caller-provided buffer.

        @param out The output buffer
    */
    void to_native(std::span<wchar_t> out) const;
#endif

    //--------------------------------------------
    //
    // Modifiers
    //
    //--------------------------------------------

    /** Clear the path.
    */
    void
    clear() noexcept
    {
        s_.clear();
    }

    /** Swap with another path.
    */
    void
    swap(path& other) noexcept
    {
        s_.swap(other.s_);
    }

    /** Append a path component with separator.

        @throws system_error if the result is invalid
    */
    path& append(path_view p);

    /** Concatenate without separator (re-validates).

        @throws system_error if the result is invalid
    */
    path& concat(std::string_view s);

    /** Append a path component with separator.

        @throws system_error if the result is invalid
    */
    path&
    operator/=(path_view p)
    {
        return append(p);
    }

    /** Concatenate without separator (re-validates).

        @throws system_error if the result is invalid
    */
    path&
    operator+=(std::string_view s)
    {
        return concat(s);
    }

    /** Remove the filename component.
    */
    path& remove_filename();

    /** Replace the filename component.

        @throws system_error if the result is invalid
    */
    path& replace_filename(path_view replacement);

    /** Replace the extension.

        @throws system_error if the result is invalid
    */
    path& replace_extension(path_view replacement = {});

    //--------------------------------------------
    //
    // Decomposition (return views into this path)
    //
    //--------------------------------------------

    /** Return the root name (e.g., "C:" on Windows).
    */
    path_view root_name() const noexcept;

    /** Return the root directory (e.g., "/").
    */
    path_view root_directory() const noexcept;

    /** Return the root path (root_name + root_directory).
    */
    path_view root_path() const noexcept;

    /** Return the path relative to the root.
    */
    path_view relative_path() const noexcept;

    /** Return the parent path.
    */
    path_view parent_path() const noexcept;

    /** Return the filename component.
    */
    path_view filename() const noexcept;

    /** Return the stem (filename without extension).
    */
    path_view stem() const noexcept;

    /** Return the extension (including the dot).
    */
    path_view extension() const noexcept;

    //--------------------------------------------
    //
    // Generation (return new paths)
    //
    //--------------------------------------------

    /** Return the path in normal form.
    */
    path lexically_normal() const;

    /** Return the path relative to a base.
    */
    path lexically_relative(path_view base) const;

    /** Return the path relative to a base, or *this if not possible.
    */
    path lexically_proximate(path_view base) const;

    //--------------------------------------------
    //
    // Query
    //
    //--------------------------------------------

    /** Return true if the path is empty.
    */
    bool
    empty() const noexcept
    {
        return s_.empty();
    }

    /** Return true if the path is absolute.
    */
    bool is_absolute() const noexcept;

    /** Return true if the path is relative.
    */
    bool is_relative() const noexcept;

    /** Return true if the path has a root name.
    */
    bool has_root_name() const noexcept;

    /** Return true if the path has a root directory.
    */
    bool has_root_directory() const noexcept;

    /** Return true if the path has a root path.
    */
    bool has_root_path() const noexcept;

    /** Return true if the path has a relative path.
    */
    bool has_relative_path() const noexcept;

    /** Return true if the path has a parent path.
    */
    bool has_parent_path() const noexcept;

    /** Return true if the path has a filename.
    */
    bool has_filename() const noexcept;

    /** Return true if the path has a stem.
    */
    bool has_stem() const noexcept;

    /** Return true if the path has an extension.
    */
    bool has_extension() const noexcept;

    //--------------------------------------------
    //
    // Component iteration (yields path_view)
    //
    //--------------------------------------------

    /** Return an iterator to the first component.
    */
    const_iterator begin() const noexcept;

    /** Return an iterator past the last component.
    */
    const_iterator end() const noexcept;

    //--------------------------------------------
    //
    // Segment iteration (yields string_view)
    //
    //--------------------------------------------

    class segment_range;

    /** Return a range of segments as string_view.

        This is lighter weight than component iteration
        since segments are simple string_views rather
        than path_views.
    */
    segment_range segments() const noexcept;

    //--------------------------------------------
    //
    // Comparison
    //
    //--------------------------------------------

    /** Compare this path to another.

        @return Negative if this < other, zero if equal, positive if this > other
    */
    int compare(path_view other) const noexcept;

    /** Return true if two paths are equal.
    */
    friend bool
    operator==(path const& lhs, path const& rhs) noexcept
    {
        return lhs.s_ == rhs.s_;
    }

    /** Three-way comparison of two paths.
    */
    friend std::strong_ordering
    operator<=>(path const& lhs, path const& rhs) noexcept
    {
        return lhs.s_ <=> rhs.s_;
    }

    /** Return true if a path equals a path_view.
    */
    friend bool
    operator==(path const& lhs, path_view rhs) noexcept
    {
        return lhs.string_view() == rhs.string_view();
    }

    /** Return true if a path_view equals a path.
    */
    friend bool
    operator==(path_view lhs, path const& rhs) noexcept
    {
        return lhs.string_view() == rhs.string_view();
    }

    /** Three-way comparison of a path and a path_view.
    */
    friend std::strong_ordering
    operator<=>(path const& lhs, path_view rhs) noexcept
    {
        return lhs.string_view() <=> rhs.string_view();
    }

    /** Three-way comparison of a path_view and a path.
    */
    friend std::strong_ordering
    operator<=>(path_view lhs, path const& rhs) noexcept
    {
        return lhs.string_view() <=> rhs.string_view();
    }

    //--------------------------------------------
    //
    // Stream
    //
    //--------------------------------------------

    /** Output the path to a stream.
    */
    friend std::ostream&
    operator<<(std::ostream& os, path const& p);

    /** Input a path from a stream.
    */
    friend std::istream&
    operator>>(std::istream& is, path& p);

private:
    friend class iterator;

    // Private unchecked constructor
    struct unchecked_t {};

    path(unchecked_t, std::string&& s) noexcept
        : s_(std::move(s))
    {
    }
};

/** Parse a string as a path without throwing.

    @return The path on success, or an error_code on failure
*/
system::result<path>
try_parse_path(std::string_view s) noexcept;

/** Parse a null-terminated string as a path without throwing.

    @return The path on success, or an error_code on failure
*/
system::result<path>
try_parse_path(char const* s) noexcept;

/** Parse and move a string as a path without throwing.

    @return The path on success, or an error_code on failure
    @note The string is moved only on success
*/
system::result<path>
try_parse_path(std::string&& s) noexcept;

//------------------------------------------------------------------------------

/** Iterator over path components (each component is a path_view).
*/
class path::iterator
{
    path const* p_ = nullptr;
    std::size_t pos_ = 0;
    std::size_t len_ = 0;

public:
    using value_type        = path_view;
    using reference         = path_view;
    using pointer           = void;
    using difference_type   = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;

    /** Default constructor.
    */
    iterator() noexcept = default;

    /** Return the current component.
    */
    path_view operator*() const noexcept;

    /** Pre-increment.
    */
    iterator& operator++() noexcept;

    /** Post-increment.
    */
    iterator operator++(int) noexcept;

    /** Pre-decrement.
    */
    iterator& operator--() noexcept;

    /** Post-decrement.
    */
    iterator operator--(int) noexcept;

    /** Return true if two iterators are equal.
    */
    friend bool
    operator==(iterator const& lhs, iterator const& rhs) noexcept
    {
        return lhs.p_ == rhs.p_ && lhs.pos_ == rhs.pos_;
    }
};

//------------------------------------------------------------------------------

/** Range over path segments as string_view.
*/
class path::segment_range
{
    path const* p_ = nullptr;

public:
    class iterator;
    using const_iterator = iterator;

    /** Return an iterator to the first segment.
    */
    iterator begin() const noexcept;

    /** Return an iterator past the last segment.
    */
    iterator end() const noexcept;

private:
    friend class path;

    explicit
    segment_range(path const* p) noexcept
        : p_(p)
    {
    }
};

/** Iterator over path segments (each segment is a string_view).
*/
class path::segment_range::iterator
{
    path const* p_ = nullptr;
    std::size_t pos_ = 0;
    std::size_t len_ = 0;

public:
    using value_type        = std::string_view;
    using reference         = std::string_view;
    using pointer           = void;
    using difference_type   = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;

    /** Default constructor.
    */
    iterator() noexcept = default;

    /** Return the current segment.
    */
    std::string_view operator*() const noexcept;

    /** Pre-increment.
    */
    iterator& operator++() noexcept;

    /** Post-increment.
    */
    iterator operator++(int) noexcept;

    /** Pre-decrement.
    */
    iterator& operator--() noexcept;

    /** Post-decrement.
    */
    iterator operator--(int) noexcept;

    /** Return true if two iterators are equal.
    */
    friend bool
    operator==(iterator const& lhs, iterator const& rhs) noexcept
    {
        return lhs.p_ == rhs.p_ && lhs.pos_ == rhs.pos_;
    }
};

//------------------------------------------------------------------------------
//
// Non-member operations
//
//------------------------------------------------------------------------------

/** Concatenate two paths with a separator.
*/
path operator/(path const& lhs, path_view rhs);

/** Concatenate two paths with a separator.
*/
path operator/(path&& lhs, path_view rhs);

/** Swap two paths.
*/
inline void
swap(path& lhs, path& rhs) noexcept
{
    lhs.swap(rhs);
}

/** Return a hash value for a path.
*/
std::size_t
hash_value(path const& p) noexcept;

/** Return a hash value for a path_view.
*/
std::size_t
hash_value(path_view p) noexcept;

} // namespace net

//------------------------------------------------------------------------------

template<>
struct std::hash<net::path>
{
    std::size_t
    operator()(net::path const& p) const noexcept
    {
        return net::hash_value(p);
    }
};

template<>
struct std::hash<net::path_view>
{
    std::size_t
    operator()(net::path_view p) const noexcept
    {
        return net::hash_value(p);
    }
};

#endif
