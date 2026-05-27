#
# Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
# Copyright (c) 2021 Dmitry Arkhipov (grisumbras@gmail.com)
# Copyright (c) 2022 Alan de Freitas (alandefreitas@gmail.com)
# Copyright (c) 2025 Mohammad Nejati
# Copyright (c) 2026 Steve Gerbino
# Copyright (c) 2026 Michael Vandeberg
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

cmake_minimum_required(VERSION 3.13...3.31)

set(BOOST_CAPY_VERSION 1)
if (BOOST_SUPERPROJECT_VERSION)
    set(BOOST_CAPY_VERSION ${BOOST_SUPERPROJECT_VERSION})
endif ()
project(boost_capy VERSION "${BOOST_CAPY_VERSION}" LANGUAGES CXX)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(BOOST_CAPY_IS_ROOT ON)
    include(CTest)
else()
    set(BOOST_CAPY_IS_ROOT OFF)
endif()
set(__ignore__ ${CMAKE_C_COMPILER})

# FreeBSD and macOS ship libc++ without full std::stop_token support;
# -fexperimental-library enables it and links libc++experimental.
include(CheckCXXCompilerFlag)
if((APPLE OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD") AND
    CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    check_cxx_compiler_flag(-fexperimental-library
        BOOST_CAPY_HAS_EXPERIMENTAL_LIBRARY)
endif()

option(BOOST_CAPY_BUILD_TESTS "Build boost::capy tests" ${BUILD_TESTING})
option(BOOST_CAPY_BUILD_EXAMPLES "Build boost::capy examples" ${BOOST_CAPY_IS_ROOT})
option(BOOST_CAPY_BUILD_BENCH "Build boost::capy benchmarks" ${BOOST_CAPY_IS_ROOT})
option(BOOST_CAPY_BUILD_P2300_EXAMPLES "Build examples that depend on beman-execution (P2300)" OFF)
option(BOOST_CAPY_BUILD_NVEXEC_EXAMPLES "Build examples that depend on NVIDIA nvexec (CUDA)" OFF)
option(BOOST_CAPY_MRDOCS_BUILD "Build the target for MrDocs: see mrdocs.yml" OFF)

if(BOOST_CAPY_BUILD_P2300_EXAMPLES)
    if(NOT DEFINED CMAKE_CXX_STANDARD OR CMAKE_CXX_STANDARD LESS 23)
        message(FATAL_ERROR
            "BOOST_CAPY_BUILD_P2300_EXAMPLES requires CMAKE_CXX_STANDARD >= 23")
    endif()
endif()

if(BOOST_CAPY_BUILD_NVEXEC_EXAMPLES)
    if(NOT BOOST_CAPY_BUILD_STDEXEC_EXAMPLES)
        message(FATAL_ERROR
            "BOOST_CAPY_BUILD_NVEXEC_EXAMPLES requires "
            "BOOST_CAPY_BUILD_STDEXEC_EXAMPLES=ON")
    endif()
    if(NOT DEFINED CMAKE_CXX_STANDARD OR CMAKE_CXX_STANDARD LESS 23)
        message(FATAL_ERROR
            "BOOST_CAPY_BUILD_NVEXEC_EXAMPLES requires CMAKE_CXX_STANDARD >= 23")
    endif()
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    # Tell NVIDIA/stdexec to build the nvexec target when its
    # FetchContent is processed (bench/ and/or the example itself).
    set(STDEXEC_ENABLE_CUDA ON CACHE BOOL
        "Build nvexec when configuring NVIDIA/stdexec" FORCE)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(BOOST_CAPY_IS_ROOT AND BUILD_SHARED_LIBS)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

file(GLOB_RECURSE BOOST_CAPY_HEADERS CONFIGURE_DEPENDS include/boost/*.hpp include/boost/*.natvis)
file(GLOB_RECURSE BOOST_CAPY_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.hpp)

source_group("" FILES "include/boost/capy.hpp" "build/Jamfile")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include/boost/capy PREFIX "include" FILES ${BOOST_CAPY_HEADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "src" FILES ${BOOST_CAPY_SOURCES})

find_package(Threads REQUIRED)

function(boost_capy_setup_properties target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
    target_include_directories(${target} PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>)
    target_compile_definitions(${target} PUBLIC BOOST_CAPY_NO_LIB)
    target_compile_definitions(${target} PRIVATE BOOST_CAPY_SOURCE)
    target_link_libraries(${target} PUBLIC Threads::Threads)
    if(BOOST_CAPY_HAS_EXPERIMENTAL_LIBRARY)
        target_compile_options(${target} PUBLIC -fexperimental-library)
        target_link_options(${target} PUBLIC -fexperimental-library)
    endif()
    target_compile_definitions(${target} PUBLIC
        $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,BOOST_CAPY_DYN_LINK,BOOST_CAPY_STATIC_LINK>)
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
    EXPORT_NAME capy
    INTERPROCEDURAL_OPTIMIZATION OFF
    INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF
    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF
    INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL OFF)

include(GNUInstallDirs)

add_subdirectory(extra/test_suite)

if(BOOST_SUPERPROJECT_VERSION AND NOT CMAKE_VERSION VERSION_LESS 3.13)
    boost_install(
        TARGETS boost_capy
        VERSION ${BOOST_SUPERPROJECT_VERSION}
        HEADER_DIRECTORY include)
else()
    include(CMakePackageConfigHelpers)

    # Set INSTALL_INTERFACE for standalone installs (boost_install handles
    # this for superproject builds, including versioned-layout paths)
    target_include_directories(boost_capy PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

    set(BOOST_CAPY_INSTALL_CMAKEDIR
        ${CMAKE_INSTALL_LIBDIR}/cmake/boost_capy)

    install(TARGETS boost_capy boost_capy_test_suite boost_capy_test_suite_main
        EXPORT boost_capy-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES extra/test_suite/test_suite.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/boost/capy/extra/test_suite)
    install(FILES
        extra/test_suite/DiscoverTests.cmake
        extra/test_suite/DiscoverAndWriteTestsScripts.cmake
        DESTINATION ${BOOST_CAPY_INSTALL_CMAKEDIR})
    install(EXPORT boost_capy-targets
        NAMESPACE Boost::
        DESTINATION ${BOOST_CAPY_INSTALL_CMAKEDIR})

    configure_package_config_file(
        cmake/boost_capy-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/boost_capy-config.cmake
        INSTALL_DESTINATION ${BOOST_CAPY_INSTALL_CMAKEDIR})
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/boost_capy-config-version.cmake
        COMPATIBILITY SameMajorVersion)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/boost_capy-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/boost_capy-config-version.cmake
        DESTINATION ${BOOST_CAPY_INSTALL_CMAKEDIR})
endif()

if(BOOST_CAPY_BUILD_BENCH)
    add_subdirectory(bench)
endif()

if (BOOST_CAPY_BUILD_EXAMPLES)
    add_subdirectory(example)
endif ()

if (BOOST_CAPY_BUILD_TESTS)
    add_subdirectory(test)
endif ()
