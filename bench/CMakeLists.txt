#
# Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES
    CMakeLists.txt bench.cpp allocation.cpp)

add_executable(boost_capy_bench bench.cpp)
target_link_libraries(boost_capy_bench PRIVATE Boost::capy)
target_include_directories(boost_capy_bench PRIVATE .)

add_executable(boost_capy_bench_allocation allocation.cpp)
target_link_libraries(boost_capy_bench_allocation PRIVATE Boost::capy)

if(BUILD_SHARED_LIBS)
    include(FetchContent)
    FetchContent_Declare(mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc
        GIT_TAG v2.2.7
        GIT_SHALLOW TRUE)
    set(MI_BUILD_TESTS OFF CACHE BOOL "Disable mimalloc tests" FORCE)
    FetchContent_MakeAvailable(mimalloc)
    target_link_libraries(boost_capy_bench_allocation PRIVATE mimalloc-static)
    target_compile_definitions(boost_capy_bench_allocation PRIVATE BOOST_CAPY_HAS_MIMALLOC=1)
endif()
