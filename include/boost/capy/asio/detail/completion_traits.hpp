//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_DETAIL_COMPLETION_TRAITS_HPP
#define BOOST_CAPY_ASIO_DETAIL_COMPLETION_TRAITS_HPP

#include <boost/capy/concept/io_runnable.hpp>
#include <boost/capy/io_result.hpp>

#include <exception>
#include <tuple>

namespace boost {
namespace capy {
namespace detail {


template<typename ResultType, bool is_noexcept> 
struct completion_traits
{
  using signature_type = void(std::exception_ptr, ResultType);
  using result_type = std::tuple<std::exception_ptr, ResultType>;        
};

template<typename ResultType> 
struct completion_traits<ResultType, true>
{
  using signature_type = void(ResultType);
  using result_type = std::tuple<ResultType>;        
};


template<typename ... Ts> 
struct completion_traits<io_result<Ts...>, true>
{
  using signature_type = void(Ts...);
  using result_type = io_result<Ts...>;        
};


template<> 
struct completion_traits<void, false>
{
  using signature_type = void(std::exception_ptr);
  using result_type = std::tuple<std::exception_ptr>;        
};

template<> 
struct completion_traits<void, true>
{
  using signature_type = void();
  using result_type = std::tuple<>;        
};

template<IoRunnable Runnable>
using completion_signature_for_io_runnable 
  = typename completion_traits<
      decltype(std::declval<Runnable&>().await_resume()),
      noexcept(std::declval<Runnable&>().await_resume())
      >::signature_type;

template<IoRunnable Runnable>
using completion_tuple_for_io_runnable 
  = typename completion_traits<
      decltype(std::declval<Runnable&>().await_resume()),
      noexcept(std::declval<Runnable&>().await_resume())
      >::result_type;


}
}
}

#endif

