//
// Copyright (c) 2026 Steve Gerbino
// Copyright (c) 2026 Michael Vandeberg
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/5.buffers/5e.rationale.adoc.

#include "../doc_warnings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include "test_suite.hpp"

namespace {

using HeaderBuffers = std::array<std::span<std::byte const>, 2>;  // 2 buffers
using BodyBuffers = std::array<std::span<std::byte const>, 3>;    // 3 buffers

// Records the buffer count so the combining fragment is observable.
std::size_t last_write_count = 0;

void write_data(std::span<std::span<std::byte const> const> buffers)
{
    last_write_count = buffers.size();
}

struct rationale_test
{
    void
    testManualCombine()
    {
        // tag::span_combine[]
        HeaderBuffers headers{ /* ... */ };
        BodyBuffers body{ /* ... */ };

        // To combine, you MUST allocate a new array:
        std::array<std::span<std::byte const>, 5> combined;
        std::copy(headers.begin(), headers.end(), combined.begin());
        std::copy(body.begin(), body.end(), combined.begin() + 2);

        write_data(combined);
        // end::span_combine[]
        BOOST_TEST(last_write_count == 5);
    }

    void
    run()
    {
        testManualCombine();
    }
};

} // namespace

TEST_SUITE(rationale_test, "boost.capy.doc.5e_rationale");
