#
# Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

file(GLOB_RECURSE PFILES CONFIGURE_DEPENDS *.cpp *.hpp)
list(APPEND PFILES CMakeLists.txt)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${PFILES})

add_executable(boost_capy_bench ${PFILES})
target_link_libraries(
    boost_capy_bench PRIVATE
    Boost::capy)

target_include_directories(boost_capy_bench PRIVATE .)
