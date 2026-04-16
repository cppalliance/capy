//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_DETAIL_FWD_HPP
#define BOOST_CAPY_ASIO_DETAIL_FWD_HPP

namespace boost::asio
{

struct execution_context;

namespace execution::detail
{

template <int I>
struct context_t;

}

template <typename T, typename Property>
struct query_result;

}


namespace asio
{

class execution_context;

namespace execution::detail
{

template <int I>
struct context_t;

}

template <typename T, typename Property>
struct query_result;

}



#endif // BOOST_CAPY_ASIO_DETAIL_FWD_HPP

