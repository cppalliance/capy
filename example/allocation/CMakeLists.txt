#
# Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

if(BUILD_SHARED_LIBS)
    include(FetchContent)
    FetchContent_Declare(mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc
        GIT_TAG v2.2.7
        GIT_SHALLOW TRUE)
    set(MI_BUILD_TESTS OFF CACHE BOOL "Disable mimalloc tests" FORCE)
    FetchContent_MakeAvailable(mimalloc)
endif()

file(GLOB_RECURSE PFILES CONFIGURE_DEPENDS *.cpp *.hpp
    CMakeLists.txt
    Jamfile)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${PFILES})

add_executable(capy_example_allocation ${PFILES})

set_property(TARGET capy_example_allocation
    PROPERTY FOLDER "examples")

target_link_libraries(capy_example_allocation Boost::capy)

if(BUILD_SHARED_LIBS)
    target_link_libraries(capy_example_allocation mimalloc-static)
    target_compile_definitions(capy_example_allocation PRIVATE BOOST_CAPY_HAS_MIMALLOC=1)
endif()
