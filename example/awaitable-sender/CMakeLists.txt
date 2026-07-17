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

add_executable(capy_example_awaitable_sender
    awaitable_sender.cpp
    awaitable_sender.hpp
    awaitable_sender_base.hpp
    awaitable_sender_detail.hpp
    read_op.hpp)

add_executable(capy_example_awaitable_sender_test
    tests.cpp
    awaitable_sender.hpp
    awaitable_sender_base.hpp
    awaitable_sender_detail.hpp
    read_op.hpp)

foreach(tgt capy_example_awaitable_sender capy_example_awaitable_sender_test)
    set_property(TARGET ${tgt} PROPERTY FOLDER "examples")
    target_compile_features(${tgt} PRIVATE cxx_std_23)
    target_link_libraries(${tgt}
        Boost::capy
        beman::execution_headers)
endforeach()

add_test(NAME capy_example_awaitable_sender_test
    COMMAND capy_example_awaitable_sender_test)
