#
# Copyright (c) 2026 Mungo Gill
# Copyright (c) 2026 Steve Gerbino
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

add_subdirectory(async-mutex)
add_subdirectory(buffer-composition)
add_subdirectory(custom-dynamic-buffer)
add_subdirectory(custom-executor)
add_subdirectory(hello-task)
add_subdirectory(mock-stream-testing)
add_subdirectory(parallel-fetch)
add_subdirectory(parallel-tasks)
add_subdirectory(producer-consumer)
add_subdirectory(strand-serialization)
add_subdirectory(stream-pipeline)
add_subdirectory(timeout-cancellation)
add_subdirectory(type-erased-echo)
add_subdirectory(when-any-cancellation)
add_subdirectory(sender-bridge)

if(TARGET Boost::asio)
    add_subdirectory(asio)
endif()

if(TARGET Boost::corosio)
    add_subdirectory(echo-server-corosio)
endif()
