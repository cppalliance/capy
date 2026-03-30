//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#if __has_include(<boost/asio.hpp>)
#include <boost/capy/asio/executor_adapter.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/execution/outstanding_work.hpp>
#endif

#if __has_include(<asio.hpp>)
#include <boost/capy/asio/standalone_adapter_standalone.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/post.hpp>
#include <asio/dispatch.hpp>
#include <asio/execution/outstanding_work.hpp>

#endif 


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

    void run() 
    {
        testExecutor();
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
    
    void run() 
    {
        testExecutor();
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
