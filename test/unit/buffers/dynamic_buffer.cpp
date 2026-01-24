//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/buffers/dynamic_buffer.hpp>

#include <boost/capy/buffers/circular_buffer.hpp>

#include "test_buffers.hpp"

namespace boost {
namespace capy {

// NOTE: any_dynamic_buffer type does not exist, test disabled
struct dynamic_buffer_concept_test
{
    // Just verify the DynamicBuffer concept compiles
    static_assert(
        is_DynamicBuffer<
            circular_buffer>::value);

    void run() {}
};

TEST_SUITE(
    dynamic_buffer_concept_test,
    "boost.capy.buffers.dynamic_buffer");

} // capy
} // boost
