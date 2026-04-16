//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//


#include <chrono>
#include <thread>
#if __has_include(<boost/asio.hpp>)
#include <boost/capy/asio/boost.hpp>


#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/execution/outstanding_work.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_future.hpp>


#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include "test_helpers.hpp"
#include "test_suite.hpp"


namespace boost {
namespace capy {

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
        auto exec = wrap_asio_executor(ctx.get_executor());

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
        auto exec = wrap_asio_executor(any_exec);
        

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

    void testTimer()
    {
        int dispatch_count = 0;
        test_executor te{dispatch_count};
        boost::capy::asio_executor_adapter wrapped_te{te};

        boost::asio::steady_timer t{wrapped_te};
        t.expires_after(std::chrono::milliseconds(1));

        bool done = false;
        t.async_wait(
            [&](auto ec)
            {
              BOOST_TEST(!ec);
              done = true;   
            });

        while (!done)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    void run() 
    {
        testExecutor();
        testFromExecutor();
        testFromAnyIOExecutor();
        testAsIoAwaitable();
        testAsioSpawn();
        testTimer();
    }
};


TEST_SUITE(
    boost_asio_test,
    "boost.capy.asio.boost");

} // namespace capy
} // namespace boost

#endif

