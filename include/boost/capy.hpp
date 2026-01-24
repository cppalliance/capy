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

#include <boost/capy/buffers.hpp>
#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/buffers/buffer_param.hpp>
#include <boost/capy/buffers/circular_dynamic_buffer.hpp>
#include <boost/capy/buffers/consuming_buffers.hpp>
#include <boost/capy/buffers/copy.hpp>
#include <boost/capy/buffers/flat_buffer.hpp>
#include <boost/capy/buffers/front.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/buffers/sink.hpp>
#include <boost/capy/buffers/slice.hpp>
#include <boost/capy/buffers/string_dynamic_buffer.hpp>
#include <boost/capy/buffers/vector_dynamic_buffer.hpp>
#include <boost/capy/concept/const_buffer_sequence.hpp>
#include <boost/capy/concept/dynamic_buffer.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/frame_allocator.hpp>
#include <boost/capy/concept/io_awaitable.hpp>
#include <boost/capy/concept/io_awaitable_task.hpp>
#include <boost/capy/concept/io_launchable_task.hpp>
#include <boost/capy/concept/mutable_buffer_sequence.hpp>
#include <boost/capy/concept/read_source.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/write_sink.hpp>
#include <boost/capy/concept/write_stream.hpp>
#include <boost/capy/cond.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/any_executor.hpp>
#include <boost/capy/ex/async_event.hpp>
#include <boost/capy/ex/async_mutex.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/ex/executor_work_guard.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/run_on.hpp>
#include <boost/capy/ex/strand.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/file.hpp>
#include <boost/capy/file_mode.hpp>
#include <boost/capy/io_awaitable.hpp>
#include <boost/capy/io_result.hpp>
#include <boost/capy/read.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/type_traits.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/write.hpp>

#endif
