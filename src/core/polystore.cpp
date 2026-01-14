//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/core/polystore.hpp>
#include <utility>

namespace boost {
namespace capy {

polystore::
~polystore()
{
    destroy();
}

polystore::
polystore(
    polystore&& other) noexcept
{
    using std::swap;
    swap(v_, other.v_);
    swap(m_, other.m_);
}

polystore&
polystore::
operator=(
    polystore&& other) noexcept
{
    if(this != &other)
    {
        using std::swap;
        polystore tmp(std::move(*this));
        swap(v_, tmp.v_);
        swap(m_, tmp.m_);
        swap(v_, other.v_);
        swap(m_, other.m_);
    }
    return *this;
}

void
polystore::
clear() noexcept
{
    destroy();
    m_.clear();
}

auto
polystore::
get_elements() noexcept ->
    elements
{
    return elements(v_.size(), *this);
}

void
polystore::
destroy() noexcept
{
    // destroy in reverse order
    for(auto n = v_.size(); n--;)
        v_.resize(n);
}

auto
polystore::
get(std::size_t i) -> any&
{
    return *v_[i];
}

void*
polystore::
find(
    core::typeinfo const& ti) const noexcept
{
    auto const it = m_.find(ti);
    if(it == m_.end())
        return nullptr;
    return it->second;
}

void*
polystore::
insert_impl(
    any_ptr p, key const* k, std::size_t n)
{
    struct do_insert
    {
        any_ptr p;
        key const* k;
        std::size_t n;
        polystore& ps;
        std::size_t i = 0;

        do_insert(
            any_ptr p_,
            key const* k_,
            std::size_t n_,
            polystore& ps_)
            : p(std::move(p_)), k(k_), n(n_), ps(ps_)
        {
            // ensure emplace_back can't fail
            ps.v_.reserve(ps.v_.size() + 1);

            for(;i < n;++i)
                if(! ps.m_.emplace(k[i].ti, k[i].p).second)
                    detail::throw_invalid_argument(
                        "polystore: duplicate key");

            ps.v_.emplace_back(std::move(p));
        }

        ~do_insert()
        {
            if(i == n)
                return;
            while(i--)
                ps.m_.erase(k[i].ti);
        }
    };

    auto const pt = p->get();
    do_insert(std::move(p), k, n, *this);
    return pt;
}

} // capy
} // boost
