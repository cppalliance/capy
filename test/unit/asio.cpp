//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//




#include <coroutine>
#if __has_include(<boost/asio.hpp>)
#include <boost/capy/asio/executor_adapter.hpp>
#include <boost/capy/asio/executor_from_asio.hpp>
#include <boost/capy/asio/as_io_awaitable.hpp>
#include <boost/capy/asio/spawn.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/execution/outstanding_work.hpp>
#include <boost/asio/use_future.hpp>
#endif

#if __has_include(<asio.hpp>)
#include <boost/capy/asio/standalone_executor_adapter.hpp>
#include <boost/capy/asio/executor_from_standalone_asio.hpp>
#include <boost/capy/asio/standalone_as_io_awaitable.hpp>
#include <boost/capy/asio/standalone_spawn.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/post.hpp>
#include <asio/dispatch.hpp>
#include <asio/execution/outstanding_work.hpp>
#include <asio/use_future.hpp>

#endif 

#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include "test_helpers.hpp"
#include "test_suite.hpp"

#include <type_traits>

namespace boost {
namespace capy {



#if __has_include(<asio.hpp>)

struct asio_standalone_test
{
    void testExecutor()
    {
        int dispatch_count = 0;
        int work_cnt = 0;
        test_executor exec{0, dispatch_count, work_cnt};
        boost::capy::standalone_asio_executor_adapter wrapped_te{exec};

        bool ran = false;
        ::asio::post(wrapped_te, [&]{ran = true;});    
        BOOST_TEST_EQ(dispatch_count, 0); 
        BOOST_TEST(ran);
        
        
        ran = false;
        ::asio::dispatch(wrapped_te, [&]{ran = true;});    
        BOOST_TEST_EQ(dispatch_count, 1); 
        BOOST_TEST(ran);

        BOOST_TEST(work_cnt == 0);
        {
            auto wk = ::asio::require(wrapped_te, ::asio::execution::outstanding_work.tracked);
            BOOST_TEST_EQ(work_cnt, 1);
            
        }
        BOOST_TEST_EQ(work_cnt, 0);
        ::asio::any_io_executor aio{wrapped_te};
        BOOST_TEST_EQ(work_cnt, 0);
        aio = ::asio::prefer(aio, ::asio::execution::outstanding_work.tracked);
        BOOST_TEST_EQ(work_cnt, 1);
        aio = nullptr;
        BOOST_TEST_EQ(work_cnt, 0);
    }

    
    void testFromExecutor()
    {
        ::asio::io_context ctx;
        boost::capy::executor_from_standalone_asio exec{ctx.get_executor()};

        bool done = false;
        auto tsk = [&]() -> boost::capy::task<void> 
                  {
                    done = true;
                    co_return ;
                  };

        boost::capy::run_async(exec)(tsk());
        BOOST_TEST(!done);
        ctx.run();
        BOOST_TEST(done);
    }

    
    void testFromAnyIOExecutor()
    {
        ::asio::io_context ctx;
        ::asio::any_io_executor any_exec{ctx.get_executor()};
        boost::capy::executor_from_standalone_asio exec{any_exec};

        bool done = false;
        auto tsk = [&]() -> boost::capy::task<void> 
                  {
                    done = true;
                    co_return ;
                  };

        boost::capy::run_async(exec)(tsk());
        BOOST_TEST(!done);
        ctx.run();
        BOOST_TEST(done);
    }

    void testAsIoAwaitable()
    {
        bool done = false;
        auto tsk = [&]() -> boost::capy::task<void> 
                  {
                    boost::capy::standalone_asio_executor_adapter wrapped_te{co_await capy::this_coro::executor};
                    co_await ::asio::post(wrapped_te, as_io_awaitable);
                    done = true;
                  };
                          int dispatch_count = 0;
        int work_cnt = 0;
        test_executor exec{0, dispatch_count, work_cnt};
        boost::capy::run_async(exec)(tsk());

        BOOST_TEST(done);
    }


    void testAsioSpawn()
    {
        int dispatch_count = 0;
        test_executor exec{0, dispatch_count};
        bool done  = false;
        auto tsk = [&]() ->
        boost::capy::task<void> 
                  {
                    done = true;
                    co_return ;
                  };

        auto ft = standalone_asio_spawn(exec, tsk(), ::asio::use_future);

        ft.get();
        BOOST_TEST(done);
        BOOST_TEST(dispatch_count == 1);
    }
    
    void run() 
    {
        testExecutor();
        testFromExecutor();
        testFromAnyIOExecutor();
        testAsIoAwaitable();
        testAsioSpawn();
    }
};

#endif 

#if __has_include(<boost/asio.hpp>)

struct boost_asio_test
{
    void testExecutor()
    {
        int dispatch_count = 0;
        int work_cnt = 0;
        test_executor exec{0, dispatch_count, work_cnt};
        boost::capy::asio_executor_adapter wrapped_te{exec};

        bool ran = false;
        boost::asio::post(wrapped_te, [&]{ran = true;});    
        BOOST_TEST_EQ(dispatch_count, 0); 
        BOOST_TEST(ran);
        
        
        ran = false;
        boost::asio::dispatch(wrapped_te, [&]{ran = true;});    
        BOOST_TEST_EQ(dispatch_count, 1); 
        BOOST_TEST(ran);

        BOOST_TEST(work_cnt == 0);
        {
            auto wk = boost::asio::require(wrapped_te, boost::asio::execution::outstanding_work.tracked);
            BOOST_TEST_EQ(work_cnt, 1);
            
        }
        BOOST_TEST_EQ(work_cnt, 0);
        boost::asio::any_io_executor aio{wrapped_te};
        BOOST_TEST_EQ(work_cnt, 0);
        aio = boost::asio::prefer(aio, boost::asio::execution::outstanding_work.tracked);
        BOOST_TEST_EQ(work_cnt, 1);
        aio = nullptr;
        BOOST_TEST_EQ(work_cnt, 0);
    }

    void testFromExecutor()
    {
        boost::asio::io_context ctx;
        boost::capy::executor_from_asio exec{ctx.get_executor()};

        bool done = false;
        auto tsk = [&]() -> boost::capy::task<void> 
                  {
                    done = true;
                    co_return ;
                  };

        boost::capy::run_async(exec)(tsk());
        BOOST_TEST(!done);
        ctx.run();
        BOOST_TEST(done);
    }

    
    void testFromAnyIOExecutor()
    {
        boost::asio::io_context ctx;
        boost::asio::any_io_executor any_exec{ctx.get_executor()};
        boost::capy::executor_from_asio exec{any_exec};

        bool done = false;
        auto tsk = [&]() -> boost::capy::task<void> 
                  {
                    done = true;
                    co_return ;
                  };

        boost::capy::run_async(exec)(tsk());
        BOOST_TEST(!done);
        ctx.run();
        BOOST_TEST(done);
    }

    void testAsIoAwaitable()
    {
    
        bool done = false;
        auto tsk = [&]() -> boost::capy::task<void> 
                  {
                    boost::capy::asio_executor_adapter wrapped_te{co_await capy::this_coro::executor};
                    
                    co_await boost::asio::post(wrapped_te, as_io_awaitable);
                    done = true;
                  };
        int dispatch_count = 0;
        int work_cnt = 0;
        test_executor exec{0, dispatch_count, work_cnt};
        boost::capy::run_async(exec)(tsk());

        BOOST_TEST(done);
    }

    void testAsioSpawn()
    {
        int dispatch_count = 0;
        test_executor exec{0, dispatch_count};
        bool done  = false;
        auto tsk = [&]() ->
        boost::capy::task<void> 
                  {
                    done = true;
                    co_return ;
                  };

        auto ft = asio_spawn(exec, tsk(), boost::asio::use_future);

        ft.get();
        BOOST_TEST(done);
        BOOST_TEST(dispatch_count == 1);
    }
    
    void run() 
    {
        testExecutor();
        testFromExecutor();
        testFromAnyIOExecutor();
        testAsIoAwaitable();
        testAsioSpawn();
    }
};

#endif

#if __has_include(<asio.hpp>)
TEST_SUITE(
    asio_standalone_test,
    "boost.capy.asio.standalone");
#endif

#if __has_include(<boost/asio.hpp>)
TEST_SUITE(
    boost_asio_test,
    "boost.capy.asio.boost");
#endif

} // namespace capy
} // namespace boost
