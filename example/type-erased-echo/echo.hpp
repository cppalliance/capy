//
// Copyright (c) 2026 Mungo Gill
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef ECHO_HPP
#define ECHO_HPP

#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/task.hpp>

namespace myapp {

// Type-erased interface: no template dependencies
boost::capy::task<> echo_session(boost::capy::any_stream& stream);

} // namespace myapp

#endif
