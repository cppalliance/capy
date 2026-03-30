//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_DETAIL_ASIO_CONTEXT_SERVICE
#define BOOST_CAPY_ASIO_DETAIL_ASIO_CONTEXT_SERVICE

#include <boost/capy/ex/execution_context.hpp>

namespace boost::capy::detail 
{

template<typename Context>
struct asio_context_service
    : Context::service
    , capy::execution_context
{
    static Context::id id;

    asio_context_service(Context & ctx) 
        : Context::service(ctx) {}
    void shutdown() override {capy::execution_context::shutdown();}
};


// asio_context_service is templated for this id.
template<typename Context>
Context::id asio_context_service<Context>::id;

}

#endif 
