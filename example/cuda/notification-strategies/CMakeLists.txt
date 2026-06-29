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
        "example/cuda/notification-strategies requires CUDA; "
        "did you set BOOST_CAPY_BUILD_CUDA_EXAMPLES?")
endif()

file(GLOB_RECURSE PFILES CONFIGURE_DEPENDS
    *.cu *.cuh *.hpp
    CMakeLists.txt
    README.md)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${PFILES})

add_executable(capy_example_cuda_notification_strategies ${PFILES})

set_target_properties(capy_example_cuda_notification_strategies PROPERTIES
    FOLDER "examples"
    CUDA_STANDARD 20
    CUDA_STANDARD_REQUIRED ON
    CUDA_SEPARABLE_COMPILATION OFF)

target_compile_features(capy_example_cuda_notification_strategies PRIVATE cxx_std_20)

target_link_libraries(capy_example_cuda_notification_strategies PRIVATE
    Boost::capy
    CUDA::cudart)
