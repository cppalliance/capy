//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#if __has_include(<asio.hpp>)
#include <boost/capy/asio/standalone.hpp>
#include <boost/capy/buffers.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/asio/stream.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/dispatch.hpp>
#include <asio/execution/outstanding_work.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_future.hpp>
#include <asio/readable_pipe.hpp>
#include <asio/connect_pipe.hpp>
#include <asio/writable_pipe.hpp>
#include <asio/write.hpp>


#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/write_stream.hpp>
#include <boost/capy/when_all.hpp>
#include "test_helpers.hpp"
#include "test_suite.hpp"


namespace boost {
namespace capy {


struct asio_standalone_test
{
    void testExecutor()
    {
        int dispatch_count = 0;
        int work_cnt = 0;
        test_executor exec{0, dispatch_count, work_cnt};
        boost::capy::asio_executor_adapter wrapped_te{exec};

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
        ::asio::io_context ctx;
        ::asio::any_io_executor any_exec{ctx.get_executor()};
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

        auto ft = asio_spawn(exec, tsk(), ::asio::use_future);

        ft.get();
        BOOST_TEST(done);
        BOOST_TEST(dispatch_count == 1);
    }

    
    void testTimer()
    {
        int dispatch_count = 0;
        test_executor te{dispatch_count};
        boost::capy::asio_executor_adapter wrapped_te{te};

        ::asio::steady_timer t{wrapped_te};
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

    void staticTestBuffer(::asio::writable_pipe & wp)
    {
        ::asio::mutable_buffer boofer;
        boost::capy::MutableBufferSequence auto seq 
            = boost::capy::as_asio_buffer_sequence(boofer);

        using seq_t = decltype(seq);

        boost::capy::ConstBufferSequence auto cseq 
            = boost::capy::as_asio_buffer_sequence(boofer);

        using cseq_t = decltype(cseq);

        std::array<boost::capy::mutable_buffer, 2u> p;
        auto seq2 = boost::capy::as_asio_buffer_sequence(p);
        using seq2_t = decltype(seq2);

        auto s = seq;
        auto cs = cseq;
        auto s2 = seq2;

        wp.write_some(seq2);

        static_assert(::asio::is_mutable_buffer_sequence<cseq_t>::value);
        static_assert(::asio::  is_const_buffer_sequence<cseq_t>::value);
    

        static_assert(::asio::is_mutable_buffer_sequence<seq_t>::value);
        static_assert(::asio::  is_const_buffer_sequence<seq_t>::value);
    
        
        static_assert(::asio::is_mutable_buffer_sequence<seq2_t>::value);
        static_assert(::asio::  is_const_buffer_sequence<seq2_t>::value);
    }



    void testStreamToAsio()
    {
        thread_pool tp;
        
        async_write_stream ws{test::write_stream(), tp.get_executor()};

        std::atomic<int> done{0};

        ::asio::async_write(
            ws,
            ::asio::buffer("Test", 4),
            [&](std::error_code ec, std::size_t n)
            {
                BOOST_TEST(!ec);
                BOOST_TEST_EQ(n, 4);
                done ++;
            });

        BOOST_TEST_EQ(ws.next_layer().data(), "Test");   

        async_read_stream rs{test::read_stream(), tp.get_executor()};
        rs.next_layer().provide("foobar");
        

        char data[100];
        ::asio::async_read(
            rs,
            ::asio::buffer(data), 
            [&](std::error_code ec, std::size_t n)
            {
                BOOST_TEST_EQ(ec, boost::capy::error::eof);
                BOOST_TEST_EQ(n, 6);
                BOOST_TEST_EQ(std::string_view(data, n), "foobar");
                done ++;
            });
            
        while (done.load() < 2u);

        tp.join();
    }


    void testStreamFromAsio()
    {
        ::asio::io_context ctx;
        ::asio::readable_pipe rp{ctx};
        ::asio::writable_pipe wp{ctx};
        ::asio::connect_pipe(rp, wp);

        any_read_stream  rs{asio_read_stream(std::move(rp)) };
        any_write_stream ws{asio_write_stream(std::move(wp))};

        bool done = false;

        auto t = 
            [&]() -> task<void>
            {
                std::string rb;
                rb.resize(10);

                auto [r1, r2, r3] = co_await capy::when_all(
                    ws.write_some(make_buffer("hello pipe", 10)),
                    rs.read_some(make_buffer(rb))
                );

                BOOST_TEST(!r1);
                BOOST_TEST_EQ(r2, r3);
                BOOST_TEST_EQ(rb, "hello pipe");
            };

        run_async(wrap_asio_executor(ctx.get_executor()), [&]{done = true;})(t());

        ctx.run();
        BOOST_TEST(done);
    }
        
    
    void run() 
    {
        testExecutor();
        testFromExecutor();
        testFromAnyIOExecutor();
        testAsIoAwaitable();
        testAsioSpawn();
        testTimer();
        testStreamToAsio();
        testStreamFromAsio();
    }
};


TEST_SUITE(
    asio_standalone_test,
    "boost.capy.asio.standalone");

} // namespace capy
} // namespace boost

#endif

