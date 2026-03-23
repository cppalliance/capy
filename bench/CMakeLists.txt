#
# Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES
    CMakeLists.txt bench.cpp allocation.cpp
    beman/main.cpp beman/beman_env.hpp beman/bench_pool.hpp)

add_executable(boost_capy_bench bench.cpp)
target_link_libraries(boost_capy_bench PRIVATE Boost::capy)
target_include_directories(boost_capy_bench PRIVATE .)

add_executable(boost_capy_bench_allocation allocation.cpp)
target_link_libraries(boost_capy_bench_allocation PRIVATE Boost::capy)

if(BOOST_CAPY_BUILD_P2300_EXAMPLES)
    include(FetchContent)
    FetchContent_Declare(
        beman-task
        GIT_REPOSITORY https://github.com/bemanproject/task
        GIT_TAG 6163df9
        SYSTEM
        FIND_PACKAGE_ARGS
            NAMES beman.task
    )
    FetchContent_MakeAvailable(beman-task)

    add_executable(boost_capy_bench_beman beman/main.cpp)
    target_compile_features(boost_capy_bench_beman PRIVATE cxx_std_23)
    target_link_libraries(boost_capy_bench_beman PRIVATE
        Boost::capy beman::task beman::execution_headers)
endif()

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
