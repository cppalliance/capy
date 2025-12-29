//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/capy
//

#ifndef BOOST_CAPY_THREAD_POOL_HPP
#define BOOST_CAPY_THREAD_POOL_HPP

#include <boost/capy/detail/config.hpp>
#include <boost/capy/executor.hpp>
#include <cstddef>

namespace boost {
namespace capy {

/** A thread pool for executing work asynchronously.

    This class provides a pool of worker threads that can
    execute submitted work items. It satisfies the requirements
    for use as an executor implementation.

    @par Thread Safety
    All member functions may be called concurrently.

    @par Example
    @code
    thread_pool pool(4);  // 4 worker threads
    executor exec = pool.get_executor();
    exec.post([]{ std::cout << "Hello from thread pool!\n"; });
    @endcode
*/
class BOOST_CAPY_DECL thread_pool
{
    class impl;
    impl* impl_;

public:
    /** Destructor.

        Signals all threads to stop and waits for them to complete.
    */
    ~thread_pool();

    /** Construct a thread pool.

        @param num_threads The number of worker threads to create.
        If zero, defaults to the hardware concurrency.
    */
    explicit
    thread_pool(std::size_t num_threads = 0);

    thread_pool(thread_pool const&) = delete;
    thread_pool& operator=(thread_pool const&) = delete;

    /** Return an executor that references this pool.

        The returned executor is a lightweight handle that
        can be copied freely. The caller must ensure this
        thread_pool outlives all executors that reference it.

        @return An executor bound to this thread pool.
    */
    executor
    get_executor() noexcept;
};

} // capy
} // boost

#endif
