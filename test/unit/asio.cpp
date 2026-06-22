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
#include <boost/capy/asio/buffers.hpp>
#include <boost/capy/asio/stream.hpp>
#include <boost/capy/ex/thread_pool.hpp>
#include <boost/capy/test/read_stream.hpp>
#include <boost/capy/test/write_stream.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/execution/outstanding_work.hpp>
#include <boost/asio/io_context.hpp>

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/connect_pipe.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/asio/use_future.hpp>

#include <boost/capy/buffers.hpp>

#include <boost/capy/when_all.hpp>


#include <boost/capy/ex/run_async.hpp>
#include <boost/capy/ex/this_coro.hpp>
#include <boost/capy/ex/executor_ref.hpp>
#include "test_helpers.hpp"
#include "test_suite.hpp"

#if defined(__GNUC__) && !defined(__clang__) && (__GNUC__ <= 16)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif


namespace boost {
namespace capy {

template<typename Aw>
struct make_awaitable_noexcept : Aw
{
  make_awaitable_noexcept(Aw&& aw) : Aw(std::move(aw)) {}
  auto await_resume() noexcept 
  {
    return Aw::await_resume();
  }
};

template<typename Stream>
struct noexcept_test_stream  : Stream
{
    noexcept_test_stream() = default;
    
    template<ConstBufferSequence CB>
    auto
    write_some(CB buffers)
    {
        return make_awaitable_noexcept(Stream::write_some(buffers));
    }
    
    template<MutableBufferSequence CB>
    auto
    read_some(CB buffers)
    {
        return make_awaitable_noexcept(Stream::read_some(buffers));
    }
};


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

        std::atomic<bool> done = false;
        t.async_wait(
            [&](auto ec)
            {
              BOOST_TEST(!ec);
              done = true;   
            });

        while (!done)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }


    void staticTestBuffer(boost::asio::writable_pipe & wp)
    {
        boost::asio::mutable_buffer boofer;
        boost::capy::MutableBufferSequence auto seq 
            = boost::capy::as_asio_buffer_sequence(boofer);

        using seq_t = decltype(seq);

        boost::capy::ConstBufferSequence auto cseq 
            = boost::capy::as_asio_buffer_sequence(boofer);

        using cseq_t = decltype(cseq);

        std::array<boost::capy::mutable_buffer, 2u> p;
        auto seq2 = boost::capy::as_asio_buffer_sequence(p);
        using seq2_t = decltype(seq2);

        [[maybe_unused]] auto s = seq;
        [[maybe_unused]] auto cs = cseq;
        [[maybe_unused]] auto s2 = seq2;

        wp.write_some(seq2);

        static_assert(boost::asio::is_mutable_buffer_sequence<cseq_t>::value);
        static_assert(boost::asio::  is_const_buffer_sequence<cseq_t>::value);
    

        static_assert(boost::asio::is_mutable_buffer_sequence<seq_t>::value);
        static_assert(boost::asio::  is_const_buffer_sequence<seq_t>::value);
    
        
        static_assert(boost::asio::is_mutable_buffer_sequence<seq2_t>::value);
        static_assert(boost::asio::  is_const_buffer_sequence<seq2_t>::value);
    }

    void testStreamToAsio()
    {
        thread_pool tp;
        
        async_write_stream ws{noexcept_test_stream<test::write_stream>(), tp.get_executor()};

        std::atomic<int> done{0};

        boost::asio::async_write(
            ws,
            boost::asio::buffer("Test", 4),
            [&](boost::system::error_code ec, std::size_t n)
            {
                BOOST_TEST(!ec);
                BOOST_TEST_EQ(n, 4);
                done ++;
            });

        BOOST_TEST_EQ(ws.next_layer().data(), "Test");   

        async_read_stream rs{noexcept_test_stream<test::read_stream>(), tp.get_executor()};
        rs.next_layer().provide("foobar");
        

        char data[100];
        boost::asio::async_read(
            rs,
            boost::asio::buffer(data), 
            [&](boost::system::error_code ec, std::size_t n)
            {
                BOOST_TEST_EQ(ec, boost::capy::error::eof);
                BOOST_TEST_EQ(n, 6);
                BOOST_TEST_EQ(std::string_view(data, n), "foobar");
                done ++;
            });
            
        while (done.load() < 2);

        tp.join();
    }

    void testStreamFromAsio()
    {
        boost::asio::io_context ctx;
        boost::asio::readable_pipe rp{ctx};
        boost::asio::writable_pipe wp{ctx};
        boost::asio::connect_pipe(rp, wp);

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
    boost_asio_test,
    "boost.capy.asio.boost");

} // namespace capy
} // namespace boost

#if defined(__GNUC__) && !defined(__clang__) && (__GNUC__ <= 16)
#pragma GCC diagnostic pop
#endif

#endif

