#
# Copyright (c) 2026 Steve Gerbino
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

add_executable(capy_example_early_quitter main.cpp quitter.hpp)
set_property(TARGET capy_example_early_quitter PROPERTY FOLDER "examples")
target_link_libraries(capy_example_early_quitter Boost::capy)

add_executable(capy_example_early_quitter_checks checks.cpp quitter.hpp)
set_property(TARGET capy_example_early_quitter_checks PROPERTY FOLDER "examples")
target_link_libraries(capy_example_early_quitter_checks Boost::capy)
add_test(NAME capy_example_early_quitter_checks
    COMMAND capy_example_early_quitter_checks)

add_executable(capy_example_early_quitter_shutdown shutdown.cpp quitter.hpp)
set_property(TARGET capy_example_early_quitter_shutdown PROPERTY FOLDER "examples")
target_link_libraries(capy_example_early_quitter_shutdown Boost::capy)
