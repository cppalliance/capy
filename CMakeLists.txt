#
# Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
# Copyright (c) 2021 Dmitry Arkhipov (grisumbras@gmail.com)
# Copyright (c) 2022 Alan de Freitas (alandefreitas@gmail.com)
# Copyright (c) 2025 Mohammad Nejati
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

#-------------------------------------------------
#
# Project
#
#-------------------------------------------------

cmake_minimum_required(VERSION 3.8...3.31)

set(BOOST_CAPY_VERSION 1)
if (BOOST_SUPERPROJECT_VERSION)
    set(BOOST_CAPY_VERSION ${BOOST_SUPERPROJECT_VERSION})
endif ()
project(boost_capy VERSION "${BOOST_CAPY_VERSION}" LANGUAGES CXX)
set(BOOST_CAPY_IS_ROOT OFF)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(BOOST_CAPY_IS_ROOT ON)
endif ()
set(__ignore__ ${CMAKE_C_COMPILER})

#-------------------------------------------------
#
# Options
#
#-------------------------------------------------
if (BOOST_CAPY_IS_ROOT)
    include(CTest)
endif ()
option(BOOST_CAPY_BUILD_TESTS "Build boost::capy tests" ${BUILD_TESTING})
option(BOOST_CAPY_BUILD_EXAMPLES "Build boost::capy examples" ${BOOST_CAPY_IS_ROOT})
option(BOOST_CAPY_MRDOCS_BUILD "Build the target for MrDocs: see mrdocs.yml" OFF)

#-------------------------------------------------
#
# Library
#
#-------------------------------------------------
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

file(GLOB_RECURSE BOOST_CAPY_HEADERS CONFIGURE_DEPENDS include/boost/*.hpp include/boost/*.natvis)
file(GLOB_RECURSE BOOST_CAPY_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.hpp)

source_group("" FILES "include/boost/capy.hpp" "build/Jamfile")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include/boost/capy PREFIX "include" FILES ${BOOST_CAPY_HEADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "src" FILES ${BOOST_CAPY_SOURCES})

function(boost_capy_setup_properties target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/include")
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_definitions(${target} PUBLIC BOOST_CAPY_NO_LIB)
    target_compile_definitions(${target} PRIVATE BOOST_CAPY_SOURCE)
    if (BUILD_SHARED_LIBS)
        target_compile_definitions(${target} PUBLIC BOOST_CAPY_DYN_LINK)
    else ()
        target_compile_definitions(${target} PUBLIC BOOST_CAPY_STATIC_LINK)
    endif ()
endfunction()

if (BOOST_CAPY_MRDOCS_BUILD)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/mrdocs.cpp"
        "#include <boost/capy.hpp>\n")
    add_library(boost_capy_mrdocs "${CMAKE_CURRENT_BINARY_DIR}/mrdocs.cpp")
    boost_capy_setup_properties(boost_capy_mrdocs)
    target_compile_definitions(boost_capy_mrdocs PUBLIC BOOST_CAPY_MRDOCS)
    set_target_properties(boost_capy_mrdocs PROPERTIES EXPORT_COMPILE_COMMANDS ON)
    return()
endif()

add_library(boost_capy include/boost/capy.hpp build/Jamfile ${BOOST_CAPY_HEADERS} ${BOOST_CAPY_SOURCES})
add_library(Boost::capy ALIAS boost_capy)
boost_capy_setup_properties(boost_capy)

# Disable IPO/LTCG - causes LNK2016 errors with MSVC
set_target_properties(boost_capy PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION OFF
    INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF
    INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL OFF)

#-------------------------------------------------
#
# Benchmarks
#
#-------------------------------------------------
add_subdirectory(bench)

#-------------------------------------------------
#
# Tests
#
#-------------------------------------------------
if (BOOST_CAPY_BUILD_TESTS)
    add_subdirectory(test)
endif ()

#-------------------------------------------------
#
# Examples
#
#-------------------------------------------------
if (BOOST_CAPY_BUILD_EXAMPLES)
    add_subdirectory(example)
endif ()
