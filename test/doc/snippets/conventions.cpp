//
// Copyright (c) 2026 Andrzej Krzemieński (akrzemi1@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/conventions.adoc.

#include "../doc_warnings.hpp"

// The page shows the aggregate header alone, then again inside the preamble
// every example assumes; nesting the tags keeps a single copy in the source.
// tag::convention[]
// tag::include_umbrella[]
#include <boost/capy.hpp>
// end::include_umbrella[]

namespace capy = boost::capy;
// end::convention[]
