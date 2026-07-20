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
add_subdirectory(custom-executor)
add_subdirectory(hello-task)
add_subdirectory(mock-stream-testing)
add_subdirectory(parallel-fetch)
add_subdirectory(parallel-tasks)
add_subdirectory(producer-consumer)
add_subdirectory(quitter-shutdown)
add_subdirectory(strand-serialization)
add_subdirectory(timeout-cancellation)
add_subdirectory(type-erased-echo)
add_subdirectory(when-any-cancellation)
if(BOOST_CAPY_BUILD_P2300_EXAMPLES)
    add_subdirectory(sender-bridge)
    add_subdirectory(awaitable-sender)
endif()

if(BOOST_CAPY_BUILD_CUDA_EXAMPLES)
    add_subdirectory(cuda/datamovement)
    add_subdirectory(cuda/notification-strategies)
    add_subdirectory(cuda/pipeline)
endif()

if(BOOST_CAPY_BUILD_NVEXEC_EXAMPLES)
endif()

add_subdirectory(fabrics)

if(TARGET Boost::asio)
    add_subdirectory(asio)
endif()

if(TARGET Boost::corosio)
    add_subdirectory(echo-server-corosio)
endif()
