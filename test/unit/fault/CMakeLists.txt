#
# Copyright (c) 2026 Michael Vandeberg
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

# The shadows replace libc functions for the whole program, so they
# link into their own executable. They are written against glibc;
# other platforms create threads differently and have no coverage here.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

file(GLOB FAULT_FILES CONFIGURE_DEPENDS *.cpp *.hpp)
list(APPEND FAULT_FILES CMakeLists.txt Jamfile)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${FAULT_FILES})

add_executable(boost_capy_fault_tests ${FAULT_FILES})
target_link_libraries(boost_capy_fault_tests PRIVATE
    Boost::capy_test_suite_main
    Boost::capy
    ${CMAKE_DL_LIBS})

# Interposition only works if the shadows land in the executable's
# dynamic symbol table. A superproject that builds everything with
# hidden visibility would strip them, and the readback then refuses
# to run rather than reporting faults that never fire.
set_target_properties(boost_capy_fault_tests PROPERTIES
    C_VISIBILITY_PRESET default
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    ENABLE_EXPORTS ON)

boost_capy_test_suite_discover_tests(boost_capy_fault_tests)
add_dependencies(tests boost_capy_fault_tests)
