//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// The compilation-firewall header shown in
// pages/6.streams/6f.isolation.adoc. The page presents this file as
// protocol.hpp; scaffolding stays outside the tags.

// tag::protocol_header[]
// protocol.hpp - No template dependencies
#pragma once
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/task.hpp>
// end::protocol_header[]

namespace capy = boost::capy;

// tag::protocol_header[]

// Declaration only - no implementation details
capy::task<> handle_protocol(capy::any_stream& stream);
// end::protocol_header[]
