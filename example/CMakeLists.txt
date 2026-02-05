#
# Copyright (c) 2026 Mungo Gill
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

add_subdirectory(hello-task)
add_subdirectory(producer-consumer)
add_subdirectory(buffer-composition)
add_subdirectory(mock-stream-testing)
add_subdirectory(type-erased-echo)
add_subdirectory(timeout-cancellation)
add_subdirectory(parallel-fetch)
add_subdirectory(custom-dynamic-buffer)
add_subdirectory(stream-pipeline)

# Requires Corosio dependency
if(TARGET Boost::corosio)
    add_subdirectory(echo-server-corosio)
endif()

add_subdirectory(asio)
