//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Test that header file is self-contained.
#include <boost/capy/io/owning.hpp>

#include <boost/capy/io/any_buffer_source.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/test/buffer_source.hpp>

#include "test/unit/test_helpers.hpp"

#include <type_traits>

namespace boost {
namespace capy {
namespace {

// Verify owning is non-copyable
static_assert(!std::is_copy_constructible_v<owning<any_buffer_source, test::buffer_source>>);
static_assert(!std::is_copy_assignable_v<owning<any_buffer_source, test::buffer_source>>);

// Verify owning is move-constructible and move-assignable
static_assert(std::is_move_constructible_v<owning<any_buffer_source, test::buffer_source>>);
static_assert(std::is_move_assignable_v<owning<any_buffer_source, test::buffer_source>>);

class owning_test
{
public:
    void
    testConstruct()
    {
        // Construct owning with forwarded arguments
        test::fuse f;
        owning<any_buffer_source, test::buffer_source> src(f);

        // Verify base class interface works
        BOOST_TEST(src.has_value());
        BOOST_TEST(static_cast<bool>(src));

        // Verify get() returns reference to owned object
        test::buffer_source& bs = src.get();
        (void)bs;
    }

    void
    testMoveConstruct()
    {
        test::fuse f;
        owning<any_buffer_source, test::buffer_source> src1(f);
        BOOST_TEST(src1.has_value());

        // Move construct
        owning<any_buffer_source, test::buffer_source> src2(std::move(src1));
        BOOST_TEST(src2.has_value());

        // Verify get() still works after move
        test::buffer_source& bs = src2.get();
        (void)bs;
    }

    void
    testMoveAssign()
    {
        test::fuse f1;
        test::fuse f2;
        owning<any_buffer_source, test::buffer_source> src1(f1);
        owning<any_buffer_source, test::buffer_source> src2(f2);
        BOOST_TEST(src1.has_value());
        BOOST_TEST(src2.has_value());

        // Move assign
        src2 = std::move(src1);
        BOOST_TEST(src2.has_value());

        // Verify get() still works after move assign
        test::buffer_source& bs = src2.get();
        (void)bs;
    }

    void
    testPolymorphism()
    {
        test::fuse f;
        owning<any_buffer_source, test::buffer_source> src(f);

        // Verify IS-A relationship
        any_buffer_source& ref = src;
        BOOST_TEST(ref.has_value());
    }

    void
    testPull()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse&) -> task<> {
            owning<any_buffer_source, test::buffer_source> src(f);
            src.get().provide("hello world");

            // Use through base class interface
            any_buffer_source& abs = src;

            const_buffer arr[detail::max_iovec_];
            auto [ec, count] = co_await abs.pull(arr, detail::max_iovec_);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(count, 1u);
            BOOST_TEST_EQ(arr[0].size(), 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    testPullAfterMove()
    {
        test::fuse f;
        auto r = f.armed([&](test::fuse& fuse) -> task<> {
            owning<any_buffer_source, test::buffer_source> src1(fuse);
            src1.get().provide("hello world");

            // Move to new location
            owning<any_buffer_source, test::buffer_source> src2(std::move(src1));

            // Use through base class interface
            any_buffer_source& abs = src2;

            const_buffer arr[detail::max_iovec_];
            auto [ec, count] = co_await abs.pull(arr, detail::max_iovec_);
            if(ec.failed())
                co_return;

            BOOST_TEST_EQ(count, 1u);
            BOOST_TEST_EQ(arr[0].size(), 11u);
        });
        BOOST_TEST(r.success);
    }

    void
    run()
    {
        testConstruct();
        testMoveConstruct();
        testMoveAssign();
        testPolymorphism();
        testPull();
        testPullAfterMove();
    }
};

TEST_SUITE(owning_test, "boost.capy.io.owning");

} // namespace
} // namespace capy
} // namespace boost
