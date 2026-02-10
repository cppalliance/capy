#
# Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

#-------------------------------------------------
#
# Find or fetch Boost.Asio
#
#-------------------------------------------------
if (NOT TARGET Boost::asio)
    # Use BOOST_SRC_DIR if already defined, else check env, else use default
    if (NOT DEFINED BOOST_SRC_DIR)
        if (DEFINED ENV{BOOST_SRC_DIR})
            set(BOOST_SRC_DIR "$ENV{BOOST_SRC_DIR}" CACHE STRING "Boost source dir")
        else ()
            set(BOOST_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." CACHE STRING "Boost source dir")
        endif ()
    endif ()

    # Find absolute BOOST_SRC_DIR
    if (NOT IS_ABSOLUTE ${BOOST_SRC_DIR})
        set(BOOST_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${BOOST_SRC_DIR}")
    endif ()

    # Validate BOOST_SRC_DIR
    set(BOOST_SRC_DIR_IS_VALID ON)
    foreach (F "CMakeLists.txt" "Jamroot" "boost-build.jam" "bootstrap.sh" "libs")
        if (NOT EXISTS "${BOOST_SRC_DIR}/${F}")
            set(BOOST_SRC_DIR_IS_VALID OFF)
            break()
        endif ()
    endforeach ()

    if (BOOST_SRC_DIR_IS_VALID)
        message(STATUS "Building with valid Boost src dir")
        # From BOOST_SRC_DIR
        set(BOOST_INCLUDE_LIBRARIES asio)
        set(BOOST_EXCLUDE_LIBRARIES capy)
        set(PREV_BUILD_TESTING ${BUILD_TESTING})
        set(BUILD_TESTING OFF CACHE BOOL "Build the tests." FORCE)
        add_subdirectory(${BOOST_SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/_deps/boost EXCLUDE_FROM_ALL)
        set(BUILD_TESTING ${PREV_BUILD_TESTING} CACHE BOOL "Build the tests." FORCE)
    else ()
        # Try installed Boost package
        find_package(Boost QUIET)
        if (Boost_FOUND)
            message(STATUS "Using installed Boost package for asio example")
            if (NOT TARGET Boost::asio)
                add_library(Boost::asio ALIAS Boost::headers)
            endif ()
        else ()
            # Fallback: FetchContent
            message(STATUS "No local Boost found, using FetchContent to download Boost")
            include(FetchContent)

            # Policy CMP0135 (introduced in CMake 3.24) controls the timestamp behavior
            # for files extracted from archives downloaded via FetchContent/ExternalProject.
            #
            # OLD behavior: Extracted files keep their original timestamps from the archive.
            #               This can cause build issues because archive timestamps are often
            #               older than dependent build files, triggering unnecessary rebuilds
            #               or causing "file has modification time in the future" warnings.
            #
            # NEW behavior: Extracted files get the current time (download time) as their
            #               timestamp, which avoids these timing-related build problems.
            #
            # We set NEW if the policy exists. On older CMake versions (< 3.24), the policy
            # doesn't exist, so this block is skipped and FetchContent works normally.
            # This avoids using DOWNLOAD_EXTRACT_TIMESTAMP directly in FetchContent_Declare,
            # which would cause a parse error on CMake < 3.24 (unrecognized keyword).
            if (POLICY CMP0135)
                cmake_policy(SET CMP0135 NEW)
            endif()

            set(BOOST_INCLUDE_LIBRARIES asio)
            FetchContent_Declare(
                boost
                URL https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-cmake.tar.xz
            )
            set(PREV_BUILD_TESTING ${BUILD_TESTING})
            set(BUILD_TESTING OFF CACHE BOOL "Build the tests." FORCE)
            FetchContent_MakeAvailable(boost)
            set(BUILD_TESTING ${PREV_BUILD_TESTING} CACHE BOOL "Build the tests." FORCE)
        endif ()
    endif ()
endif ()

#-------------------------------------------------
#
# Example executables
#
#-------------------------------------------------
file(GLOB_RECURSE PFILES CONFIGURE_DEPENDS *.cpp *.hpp
    CMakeLists.txt
    Jamfile)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${PFILES})

# any_stream example (coroutine-based)
add_executable(capy_example_any_stream
    any_stream.cpp
    api/capy_streams.cpp
    api/capy_streams.hpp)

set_property(TARGET capy_example_any_stream
    PROPERTY FOLDER "examples")

target_link_libraries(capy_example_any_stream
    Boost::capy
    Boost::asio)

# asio_callbacks example (callback-based with uni_stream)
add_executable(capy_example_asio_callbacks
    asio_callbacks.cpp
    api/capy_streams.cpp
    api/capy_streams.hpp
    api/uni_stream.hpp)

set_property(TARGET capy_example_asio_callbacks
    PROPERTY FOLDER "examples")

target_link_libraries(capy_example_asio_callbacks
    Boost::capy
    Boost::asio)

# use_capy example (use_capy completion token)
add_executable(capy_example_use_capy
    use_capy_example.cpp
    api/capy_streams.cpp
    api/capy_streams.hpp
    api/use_capy.hpp)

set_property(TARGET capy_example_use_capy
    PROPERTY FOLDER "examples")

target_link_libraries(capy_example_use_capy
    Boost::capy
    Boost::asio)
