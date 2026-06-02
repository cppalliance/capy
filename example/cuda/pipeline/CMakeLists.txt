#
# Copyright (c) 2026 Steve Gerbino
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

# CUDA was enabled at the top level when the option was flipped on.
# Honor a clean error if the user wired around it.
if(NOT CMAKE_CUDA_COMPILER)
    message(FATAL_ERROR
        "example/cuda-pipeline requires CUDA; "
        "did you set BOOST_CAPY_BUILD_NVEXEC_EXAMPLES?")
endif()

# Fetch NVIDIA/stdexec independently of bench so the example builds
# even with BOOST_CAPY_BUILD_BENCH=OFF. If bench has already declared
# the same content with the same name, this call is a no-op.
include(FetchContent)
FetchContent_Declare(
    stdexec
    GIT_REPOSITORY https://github.com/NVIDIA/stdexec
    GIT_TAG        307b83c5689ea7c2e5b31561cdc428697705333e
    SYSTEM
    FIND_PACKAGE_ARGS NAMES stdexec
)
FetchContent_MakeAvailable(stdexec)

if(NOT TARGET STDEXEC::nvexec)
    message(FATAL_ERROR
        "STDEXEC::nvexec target not found after configuring stdexec. "
        "Ensure CUDA is enabled and STDEXEC_ENABLE_CUDA=ON.")
endif()

file(GLOB_RECURSE PFILES CONFIGURE_DEPENDS
    *.cu *.cuh *.hpp
    CMakeLists.txt
    README.md)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${PFILES})

add_executable(capy_example_cuda_pipeline ${PFILES})

set_target_properties(capy_example_cuda_pipeline PROPERTIES
    FOLDER "examples"
    CUDA_STANDARD 20
    CUDA_STANDARD_REQUIRED ON
    CUDA_SEPARABLE_COMPILATION OFF)

target_compile_features(capy_example_cuda_pipeline PRIVATE cxx_std_23)

target_link_libraries(capy_example_cuda_pipeline PRIVATE
    Boost::capy
    STDEXEC::stdexec
    STDEXEC::nvexec
    CUDA::cudart)
