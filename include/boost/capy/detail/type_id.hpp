//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_DETAIL_TYPE_ID_HPP
#define BOOST_CAPY_DETAIL_TYPE_ID_HPP

#include <boost/capy/detail/config.hpp>

#if BOOST_CAPY_NO_RTTI

//------------------------------------------------
// Custom implementation (no RTTI)
//------------------------------------------------

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace boost {
namespace capy {
namespace detail {

template<typename T>
constexpr std::string_view
type_name_impl() noexcept
{
    constexpr auto strlen_ce = [](char const* s) constexpr noexcept {
        std::size_t n = 0;
        while(s[n] != '\0')
            ++n;
        return n;
    };

    constexpr auto find = [](char const* s, std::size_t len, char c) constexpr noexcept {
        for(std::size_t i = 0; i < len; ++i)
            if(s[i] == c)
                return i;
        return len;
    };

    constexpr auto rfind = [](char const* s, std::size_t len, char c) constexpr noexcept {
        for(std::size_t i = len; i > 0; --i)
            if(s[i - 1] == c)
                return i - 1;
        return len;
    };

#if defined(__clang__)
    constexpr char const* s = __PRETTY_FUNCTION__;
    constexpr std::size_t len = strlen_ce(s);
    constexpr std::size_t start = find(s, len, '=') + 2;
    constexpr std::size_t end = rfind(s, len, ']');
    return {s + start, end - start};

#elif defined(__GNUC__)
    constexpr char const* s = __PRETTY_FUNCTION__;
    constexpr std::size_t len = strlen_ce(s);
    constexpr std::size_t start = find(s, len, '=') + 2;
    constexpr std::size_t end = rfind(s, len, ']');
    return {s + start, end - start};

#elif defined(_MSC_VER)
    constexpr char const* s = __FUNCSIG__;
    constexpr std::size_t len = strlen_ce(s);
    constexpr std::size_t start = find(s, len, '<') + 1;
    constexpr std::size_t end = rfind(s, len, '>');
    return {s + start, end - start};

#else
    return "unknown";
#endif
}

template<typename T>
inline constexpr std::string_view type_name = type_name_impl<T>();

class type_info
{
    void const* id_;
    std::string_view name_;

    constexpr explicit
    type_info(void const* id, std::string_view name) noexcept
        : id_(id)
        , name_(name)
    {
    }

    template<typename T>
    friend struct type_id_impl;

public:
    type_info(type_info const&) = default;
    type_info& operator=(type_info const&) = default;

    constexpr std::string_view
    name() const noexcept
    {
        return name_;
    }

    constexpr bool
    operator==(type_info const& rhs) const noexcept
    {
        return id_ == rhs.id_;
    }

    constexpr bool
    operator!=(type_info const& rhs) const noexcept
    {
        return id_ != rhs.id_;
    }

    constexpr bool
    before(type_info const& rhs) const noexcept
    {
        return id_ < rhs.id_;
    }

    std::uintptr_t
    hash_code() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(id_);
    }
};

template<typename T>
struct type_id_impl
{
    inline static constexpr char tag{};
    inline static constexpr type_info value{&tag, type_name<T>};
};

/// Returns type_info for type T (ignores top-level cv-qualifiers)
template<typename T>
inline constexpr type_info const&
type_id() noexcept
{
    return type_id_impl<std::remove_cv_t<T>>::value;
}

class type_index
{
    type_info const* info_;

public:
    constexpr
    type_index(type_info const& info) noexcept
        : info_(&info)
    {
    }

    constexpr std::string_view
    name() const noexcept
    {
        return info_->name();
    }

    std::uintptr_t
    hash_code() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(info_);
    }

    constexpr bool
    operator==(type_index const& rhs) const noexcept = default;

    constexpr std::strong_ordering
    operator<=>(type_index const& rhs) const noexcept = default;
};

} // detail
} // capy
} // boost

template<>
struct std::hash<boost::capy::detail::type_index>
{
    std::size_t
    operator()(boost::capy::detail::type_index const& ti) const noexcept
    {
        return ti.hash_code();
    }
};

#else // BOOST_CAPY_NO_RTTI

//------------------------------------------------
// Standard RTTI implementation
//------------------------------------------------

#include <typeinfo>
#include <typeindex>

namespace boost {
namespace capy {
namespace detail {

using type_info = std::type_info;
using type_index = std::type_index;

/// Returns type_info for type T
template<typename T>
inline type_info const&
type_id() noexcept
{
    return typeid(T);
}

} // detail
} // capy
} // boost

#endif // BOOST_CAPY_NO_RTTI

#endif
