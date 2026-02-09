//
// Copyright (c) 2025 Mohammad Nejati
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_HPP
#define BOOST_CAPY_HPP

/** @file
    @brief Master header including all public capy components.

    Including this header provides access to the complete capy library:
    coroutine tasks, executors, I/O operations, buffers, and concepts.
*/

// Core types
#include <boost/capy/cond.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/io_task.hpp>
#include <boost/capy/task.hpp>

// Algorithms
#include <boost/capy/read.hpp>
#include <boost/capy/read_until.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/when_any.hpp>
#include <boost/capy/write.hpp>

// Buffers
#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_copy.hpp>
#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/buffers/buffer_param.hpp>
#include <boost/capy/buffers/circular_dynamic_buffer.hpp>
#include <boost/capy/buffers/consuming_buffers.hpp>
#include <boost/capy/buffers/flat_dynamic_buffer.hpp>
#include <boost/capy/buffers/front.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/buffers/slice.hpp>
#include <boost/capy/buffers/buffer_array.hpp>
#include <boost/capy/buffers/string_dynamic_buffer.hpp>
#include <boost/capy/buffers/vector_dynamic_buffer.hpp>

// Concepts
#include <boost/capy/concept/buffer_archetype.hpp>
#include <boost/capy/concept/buffer_sink.hpp>
#include <boost/capy/concept/buffer_source.hpp>
#include <boost/capy/concept/const_buffer_sequence.hpp>
#include <boost/capy/concept/decomposes_to.hpp>
#include <boost/capy/concept/dynamic_buffer.hpp>
#include <boost/capy/concept/execution_context.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/io_awaitable_task.hpp>
#include <boost/capy/concept/io_launchable_task.hpp>
#include <boost/capy/concept/match_condition.hpp>
#include <boost/capy/concept/mutable_buffer_sequence.hpp>
#include <boost/capy/concept/read_source.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/stream.hpp>
#include <boost/capy/concept/write_sink.hpp>
#include <boost/capy/concept/write_stream.hpp>

// Execution
#include <boost/capy/ex/any_executor.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/async_mutex.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/work_guard.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/immediate.hpp>
#include <boost/capy/ex/io_awaitable_support.hpp>
#include <boost/capy/ex/recycling_memory_resource.hpp>
#include <boost/capy/ex/run.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/strand.hpp>
#include <boost/capy/ex/system_context.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/thread_pool.hpp>

// Type-erased I/O
#include <boost/capy/io/any_buffer_sink.hpp>
#include <boost/capy/io/any_buffer_source.hpp>
#include <boost/capy/io/any_read_source.hpp>
#include <boost/capy/io/any_read_stream.hpp>
#include <boost/capy/io/any_stream.hpp>
#include <boost/capy/io/any_write_sink.hpp>
#include <boost/capy/io/any_write_stream.hpp>
#include <boost/capy/io/pull_from.hpp>
#include <boost/capy/io/push_to.hpp>

#endif
