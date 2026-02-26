#
# Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

# This file defines a modular target for the Capy test suite.
# It can be used in other projects or subdirectories that want to
# define test targets without external dependencies.

cmake_minimum_required(VERSION 3.8...3.20)

# Library target with test_suite.cpp and test_suite.hpp
add_library(boost_capy_test_suite STATIC test_suite.cpp test_suite.hpp)
target_include_directories(boost_capy_test_suite PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/boost/capy/extra/test_suite>)
target_compile_features(boost_capy_test_suite PUBLIC cxx_std_17)

# Library target that links the previous and adds test_main.cpp
add_library(boost_capy_test_suite_main STATIC test_main.cpp)
target_link_libraries(boost_capy_test_suite_main PUBLIC boost_capy_test_suite)
target_include_directories(boost_capy_test_suite_main PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/boost/capy/extra/test_suite>)
target_compile_features(boost_capy_test_suite_main PUBLIC cxx_std_17)

set_target_properties(boost_capy_test_suite PROPERTIES
    EXPORT_NAME capy_test_suite)
set_target_properties(boost_capy_test_suite_main PROPERTIES
    EXPORT_NAME capy_test_suite_main)

add_library(Boost::capy_test_suite ALIAS boost_capy_test_suite)
add_library(Boost::capy_test_suite_main ALIAS boost_capy_test_suite_main)

include(DiscoverTests.cmake)
