//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/detail/frame_memory_resource.hpp>

#include <cstddef>
#include <memory>

#include "test_suite.hpp"

namespace boost {
namespace capy {
namespace detail {

class frame_memory_resource_test
{
public:
    void
    run()
    {
        frame_memory_resource<std::allocator<int>> fmr{
            std::allocator<int>{}};

        std::pmr::memory_resource* mr = fmr.get();
        BOOST_TEST(mr == &fmr);

        // do_allocate / do_deallocate round trip through the resource.
        void* p = mr->allocate(64, alignof(std::max_align_t));
        BOOST_TEST(p != nullptr);
        mr->deallocate(p, 64, alignof(std::max_align_t));

        // do_is_equal: identity only.
        frame_memory_resource<std::allocator<int>> other{
            std::allocator<int>{}};
        BOOST_TEST(mr->is_equal(*mr));
        BOOST_TEST(! mr->is_equal(*other.get()));
    }
};

TEST_SUITE(frame_memory_resource_test, "boost.capy.detail.frame_memory_resource");

} // detail
} // capy
} // boost
