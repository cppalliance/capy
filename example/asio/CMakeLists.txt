#
# Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

# use_capy example (use_capy completion token)
add_executable(capy_example_asio_echo_server
    echo_server.cpp)

set_property(TARGET capy_example_asio_echo_server
    PROPERTY FOLDER "examples")

target_link_libraries(capy_example_asio_echo_server
    Boost::capy
    Boost::asio)
