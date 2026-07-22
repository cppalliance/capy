//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Compiled fragments shown in pages/5.buffers/5a.overview.adoc.

// Fragments deliberately leave results and bindings unused; the pages
// explain the values in prose instead.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-function"
// gcc 15 with sanitizers misattributes coroutine frame delete paths
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
#pragma clang diagnostic ignored "-Wunused-private-field"
#endif
#if defined(_MSC_VER)
#pragma warning(disable: 4834) // discarding [[nodiscard]] return value
#pragma warning(disable: 4189) // local variable initialized but not referenced
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4101) // unreferenced local variable
#pragma warning(disable: 4456) // declaration hides previous local declaration
#pragma warning(disable: 4457) // declaration hides function parameter
#pragma warning(disable: 4458) // declaration hides class member
#pragma warning(disable: 4459) // declaration hides global declaration
#endif

#include <boost/capy/buffers.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include "test_suite.hpp"

namespace {

using namespace boost::capy;

// tag::span_signatures[]
void write_data(std::span<std::byte const> data);
void read_data(std::span<std::byte> buffer);
// end::span_signatures[]

// tag::span_of_spans[]
void write_data(std::span<std::span<std::byte const> const> buffers);
// end::span_of_spans[]

// tag::span_aliases[]
using HeaderBuffers = std::array<std::span<std::byte const>, 2>;  // 2 buffers
using BodyBuffers = std::array<std::span<std::byte const>, 3>;    // 3 buffers
// end::span_aliases[]

// tag::concept_signature[]
template<ConstBufferSequence Buffers>
void write_data(Buffers const& buffers);
// end::concept_signature[]

// Definitions for the declared signatures; the fragments only show
// the declarations.
[[maybe_unused]] void write_data(std::span<std::byte const>)
{
}

[[maybe_unused]] void read_data(std::span<std::byte>)
{
}

// Records the buffer count so the combining fragment is observable.
std::size_t last_write_count = 0;

void write_data(std::span<std::span<std::byte const> const> buffers)
{
    last_write_count = buffers.size();
}

struct overview_test
{
    void testManualCombine()
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

    void run()
    {
        testManualCombine();
    }
};

} // namespace

TEST_SUITE(overview_test, "boost.capy.doc.5a_overview");
