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

#include <boost/capy/any_dispatcher.hpp>
#include <boost/capy/application.hpp>
#include <boost/capy/async_op.hpp>
#include <boost/capy/async_run.hpp>
#include <boost/capy/bcrypt.hpp>
#include <boost/capy/brotli.hpp>
#include <boost/capy/buffers/any_buffers.hpp>
#include <boost/capy/buffers/any_read_source.hpp>
#include <boost/capy/buffers/any_sink.hpp>
#include <boost/capy/buffers/any_source.hpp>
#include <boost/capy/buffers/any_stream.hpp>
#include <boost/capy/buffers/buffer.hpp>
#include <boost/capy/buffers/buffer_pair.hpp>
#include <boost/capy/buffers/circular_buffer.hpp>
#include <boost/capy/buffers/copy.hpp>
#include <boost/capy/buffers/data_source.hpp>
#include <boost/capy/buffers/dynamic_buffer.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/buffers/flat_buffer.hpp>
#include <boost/capy/buffers/front.hpp>
#include <boost/capy/buffers/make_buffer.hpp>
#include <boost/capy/buffers/range.hpp>
#include <boost/capy/buffers/read_source.hpp>
#include <boost/capy/buffers/slice.hpp>
#include <boost/capy/buffers/string_buffer.hpp>
#include <boost/capy/buffers/to_string.hpp>
#include <boost/capy/concept/affine_awaitable.hpp>
#include <boost/capy/concept/data_source.hpp>
#include <boost/capy/concept/dispatcher.hpp>
#include <boost/capy/concept/dynamic_buffer.hpp>
#include <boost/capy/concept/executor.hpp>
#include <boost/capy/concept/frame_allocator.hpp>
#include <boost/capy/concept/read_stream.hpp>
#include <boost/capy/concept/stoppable_awaitable.hpp>
#include <boost/capy/config.hpp>
#include <boost/capy/coro.hpp>
#include <boost/capy/datastore.hpp>
#include <boost/capy/embed.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/execution_context.hpp>
#include <boost/capy/executor_work_guard.hpp>
#include <boost/capy/file.hpp>
#include <boost/capy/file_mode.hpp>
#include <boost/capy/frame_allocator.hpp>
#include <boost/capy/intrusive_list.hpp>
#include <boost/capy/intrusive_queue.hpp>
#include <boost/capy/make_affine.hpp>
#include <boost/capy/neunique_ptr.hpp>
#include <boost/capy/polystore.hpp>
#include <boost/capy/polystore_fwd.hpp>
#include <boost/capy/run_on.hpp>
#include <boost/capy/small_unique_ptr.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/thread_local_ptr.hpp>
#include <boost/capy/thread_pool.hpp>
#include <boost/capy/zlib.hpp>

#endif
