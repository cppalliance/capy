#
# Copyright (c) 2026 Steve Gerbino
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

include(FetchContent)

FetchContent_Declare(
    stdexec
    GIT_REPOSITORY https://github.com/NVIDIA/stdexec
    GIT_TAG 307b83c5689ea7c2e5b31561cdc428697705333e
    SYSTEM
    FIND_PACKAGE_ARGS
        NAMES stdexec
)
FetchContent_MakeAvailable(stdexec)

add_executable(capy_example_any_sender_size any_sender_size.cpp)

set_property(TARGET capy_example_any_sender_size PROPERTY FOLDER "examples")
target_compile_features(capy_example_any_sender_size PRIVATE cxx_std_23)
target_link_libraries(capy_example_any_sender_size STDEXEC::stdexec)

add_test(NAME capy_example_any_sender_size
    COMMAND capy_example_any_sender_size)
