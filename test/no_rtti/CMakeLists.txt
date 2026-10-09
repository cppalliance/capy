#
# Copyright (c) 2026 Michael Vandeberg
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

# Built with RTTI disabled. The capy library it links is not, which is
# why any_executor.cpp may only type-erase executor types it defines.
add_executable(boost_capy_tests_no_rtti any_executor.cpp)
target_link_libraries(
    boost_capy_tests_no_rtti PRIVATE
    Boost::capy_test_suite_main
    Boost::capy)

if(MSVC)
    target_compile_options(boost_capy_tests_no_rtti PRIVATE /GR-)
else()
    target_compile_options(boost_capy_tests_no_rtti PRIVATE -fno-rtti)
endif()

boost_capy_test_suite_discover_tests(boost_capy_tests_no_rtti)

add_dependencies(tests boost_capy_tests_no_rtti)
