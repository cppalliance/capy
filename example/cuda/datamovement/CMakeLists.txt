#
# Copyright (c) 2026 Steve Gerbino
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

# CUDA was enabled at the top level when the option was flipped on.
if(NOT CMAKE_CUDA_COMPILER)
    message(FATAL_ERROR
        "example/cuda/datamovement requires CUDA; "
        "did you set BOOST_CAPY_BUILD_CUDA_EXAMPLES?")
endif()

file(GLOB_RECURSE PFILES CONFIGURE_DEPENDS
    *.cu *.cuh *.hpp
    CMakeLists.txt
    README.md)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${PFILES})

add_executable(capy_example_cuda_datamovement ${PFILES})

set_target_properties(capy_example_cuda_datamovement PROPERTIES
    FOLDER "examples"
    CUDA_STANDARD 20
    CUDA_STANDARD_REQUIRED ON
    CUDA_SEPARABLE_COMPILATION OFF)

target_compile_features(capy_example_cuda_datamovement PRIVATE cxx_std_20)

target_link_libraries(capy_example_cuda_datamovement PRIVATE
    Boost::capy
    CUDA::cudart)

# The NCCL interop snippet compiles only when NCCL is available.
# Without it, the rest of the example still builds.
find_path(CAPY_NCCL_INCLUDE_DIR nccl.h)
find_library(CAPY_NCCL_LIBRARY nccl)
if(CAPY_NCCL_INCLUDE_DIR AND CAPY_NCCL_LIBRARY)
    target_include_directories(capy_example_cuda_datamovement PRIVATE
        ${CAPY_NCCL_INCLUDE_DIR})
    target_link_libraries(capy_example_cuda_datamovement PRIVATE
        ${CAPY_NCCL_LIBRARY})
    target_compile_definitions(capy_example_cuda_datamovement PRIVATE
        CAPY_EXAMPLE_HAS_NCCL=1)
    message(STATUS "cuda/datamovement: NCCL found; building NCCL interop")
else()
    message(STATUS "cuda/datamovement: NCCL not found; skipping NCCL interop")
endif()
