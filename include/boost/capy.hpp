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

#include <boost/capy/ex/any_dispatcher.hpp>
#include <boost/capy/application.hpp>
#include <boost/capy/ex/async_op.hpp>
#include <boost/capy/ex/async_run.hpp>
#include <boost/capy/bcrypt.hpp>
#include <boost/capy/brotli.hpp>
#include <boost/capy/buffers/any_read_source.hpp>
#include <boost/capy/buffers/any_sink.hpp>
#include <boost/capy/buffers/any_stream.hpp>
#include <boost/capy/buffers.hpp>
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
#include <boost/capy/detail/config.hpp>
#include <boost/capy/ex/any_coro.hpp>
#include <boost/capy/datastore.hpp>
#include <boost/capy/core/embed.hpp>
#include <boost/capy/error.hpp>
#include <boost/capy/ex/execution_context.hpp>
#include <boost/capy/ex/executor_work_guard.hpp>
#include <boost/capy/file.hpp>
#include <boost/capy/file_mode.hpp>
#include <boost/capy/ex/frame_allocator.hpp>
#include <boost/capy/core/intrusive_list.hpp>
#include <boost/capy/core/intrusive_queue.hpp>
#include <boost/capy/ex/make_affine.hpp>
#include <boost/capy/core/neunique_ptr.hpp>
#include <boost/capy/core/polystore.hpp>
#include <boost/capy/core/polystore_fwd.hpp>
#include <boost/capy/ex/run_on.hpp>
#include <boost/capy/core/small_unique_ptr.hpp>
#include <boost/capy/strand.hpp>
#include <boost/capy/task.hpp>
#include <boost/capy/core/thread_local_ptr.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/when_all.hpp>
#include <boost/capy/zlib.hpp>

#endif
