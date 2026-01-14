//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/brotli/shared_dictionary.hpp>
#include <boost/capy/core/polystore.hpp>

#if 0
#include <brotli/shared_dictionary.h>
#endif

namespace boost {
namespace capy {
namespace brotli {

class shared_dictionary_service_impl
    : public shared_dictionary_service
{
public:
    using key_type = shared_dictionary_service;

    explicit
    shared_dictionary_service_impl(
        capy::polystore&) noexcept
    {
    }

    ~shared_dictionary_service_impl()
    {
    }
#if 0
    shared_dictionary*
    create_instance(
        alloc_func alloc_func,
        free_func free_func,
        void* opaque) const noexcept override
    {
        return reinterpret_cast<shared_dictionary*>(
            BrotliSharedDictionaryCreateInstance(
                alloc_func,
                free_func,
                opaque));
    }

    void
    destroy_instance(
        shared_dictionary* dict) const noexcept override
    {
        BrotliSharedDictionaryDestroyInstance(
            reinterpret_cast<BrotliSharedDictionary*>(dict));
    }

    bool
    attach(
        shared_dictionary* dict,
        shared_dictionary_type type,
        std::size_t data_size,
        const uint8_t data[]) const noexcept override
    {
        return BrotliSharedDictionaryAttach(
            reinterpret_cast<BrotliSharedDictionary*>(dict),
            static_cast<BrotliSharedDictionaryType>(type),
            data_size,
            data);
    }
#endif
};

shared_dictionary_service&
install_shared_dictionary_service(polystore& ctx)
{
    return ctx.emplace<shared_dictionary_service_impl>(ctx);
}

} // brotli
} // capy
} // boost
