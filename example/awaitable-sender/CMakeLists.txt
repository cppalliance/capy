#
# Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

include(FetchContent)

FetchContent_Declare(
    execution
    GIT_REPOSITORY https://github.com/bemanproject/execution.git
    GIT_TAG main
    SYSTEM
)
FetchContent_MakeAvailable(execution)

file(GLOB_RECURSE PFILES CONFIGURE_DEPENDS *.cpp *.hpp
    CMakeLists.txt
    Jamfile)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${PFILES})

add_executable(capy_example_awaitable_sender ${PFILES})

set_property(TARGET capy_example_awaitable_sender
    PROPERTY FOLDER "examples")

target_compile_features(capy_example_awaitable_sender
    PRIVATE cxx_std_23)

target_link_libraries(capy_example_awaitable_sender
    Boost::capy
    beman::execution_headers)
