#
# Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

file(GLOB_RECURSE PFILES CONFIGURE_DEPENDS *.cpp *.hpp)
list(APPEND PFILES
    CMakeLists.txt
    Jamfile)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${PFILES})

add_executable(boost_capy_tests ${PFILES})
target_link_libraries(
    boost_capy_tests PRIVATE
    Boost::capy_test_suite_main
    Boost::capy)

target_include_directories(boost_capy_tests PRIVATE . ../../)

if(TARGET Boost::asio)
    target_link_libraries(boost_capy_tests PRIVATE Boost::asio)
    target_compile_definitions(boost_capy_tests PRIVATE BOOST_CAPY_HAS_ASIO=1)
endif()

# Register individual tests with CTest
boost_capy_test_suite_discover_tests(boost_capy_tests)

# Add the main test target to Boost's test suite
add_dependencies(tests boost_capy_tests)
