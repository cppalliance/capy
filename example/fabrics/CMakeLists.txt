#
# Copyright (c) 2026 Steve Gerbino
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

# Transport-neutral, non-GPU listings from P4251R0: the byte-oriented
# compound-result pattern (capy only) and the HPC-fabric send signatures.
# No CUDA.

file(GLOB_RECURSE PFILES CONFIGURE_DEPENDS
    *.cpp *.hpp
    CMakeLists.txt
    README.md)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${PFILES})

add_executable(capy_example_fabrics ${PFILES})

set_target_properties(capy_example_fabrics PROPERTIES FOLDER "examples")

target_compile_features(capy_example_fabrics PRIVATE cxx_std_20)

target_link_libraries(capy_example_fabrics PRIVATE Boost::capy)

# HPC-fabric signature checks: each call is compiled (never run) against the real
# library header only when that library is found, to verify the paper's
# signature. Each library is optional and independent.
find_path(CAPY_IBVERBS_INCLUDE_DIR infiniband/verbs.h)
find_library(CAPY_IBVERBS_LIBRARY ibverbs)
if(CAPY_IBVERBS_INCLUDE_DIR AND CAPY_IBVERBS_LIBRARY)
    target_include_directories(capy_example_fabrics PRIVATE
        ${CAPY_IBVERBS_INCLUDE_DIR})
    target_link_libraries(capy_example_fabrics PRIVATE
        ${CAPY_IBVERBS_LIBRARY})
    target_compile_definitions(capy_example_fabrics PRIVATE
        CAPY_EXAMPLE_HAS_IBVERBS=1)
    message(STATUS "fabrics: libibverbs found; verifying ibv_post_send")
endif()

find_path(CAPY_LIBFABRIC_INCLUDE_DIR rdma/fabric.h)
find_library(CAPY_LIBFABRIC_LIBRARY fabric)
if(CAPY_LIBFABRIC_INCLUDE_DIR AND CAPY_LIBFABRIC_LIBRARY)
    target_include_directories(capy_example_fabrics PRIVATE
        ${CAPY_LIBFABRIC_INCLUDE_DIR})
    target_link_libraries(capy_example_fabrics PRIVATE
        ${CAPY_LIBFABRIC_LIBRARY})
    target_compile_definitions(capy_example_fabrics PRIVATE
        CAPY_EXAMPLE_HAS_LIBFABRIC=1)
    message(STATUS "fabrics: libfabric found; verifying fi_send")
endif()

# UCX ships in the official 'openucx' package; it is also bundled in the
# NVIDIA HPC SDK. Pass -DCAPY_UCX_INCLUDE_DIR/-DCAPY_UCX_LIBRARY to point at
# a non-standard location.
find_path(CAPY_UCX_INCLUDE_DIR ucp/api/ucp.h)
find_library(CAPY_UCX_LIBRARY ucp)
if(CAPY_UCX_INCLUDE_DIR AND CAPY_UCX_LIBRARY)
    target_include_directories(capy_example_fabrics PRIVATE
        ${CAPY_UCX_INCLUDE_DIR})
    target_link_libraries(capy_example_fabrics PRIVATE
        ${CAPY_UCX_LIBRARY})
    target_compile_definitions(capy_example_fabrics PRIVATE
        CAPY_EXAMPLE_HAS_UCX=1)
    message(STATUS "fabrics: UCX found; verifying ucp_tag_send_nbx")
endif()
