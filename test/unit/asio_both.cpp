
#if __has_include(<asio.hpp>) && __has_include(<boost/asio.hpp>)

#include <boost/capy/asio/boost.hpp>
#include <boost/capy/asio/standalone.hpp>


#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>

#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>

#include "test_helpers.hpp"
#include "test_suite.hpp"

namespace boost {
namespace capy {


struct boost_asio_both_test
{

  boost::asio::awaitable<int> aw_boost(executor_ref exec, task<int> t)
  {
    int i = co_await asio_spawn(exec, std::move(t));
    BOOST_TEST(i == 42);
    co_return i;
  }

  
  ::asio::awaitable<int> aw_standalone(executor_ref exec, task<int> t)
  {
    int i = co_await asio_spawn(exec, std::move(t));
    BOOST_TEST(i == 42);
    co_return i;
  }

  
  boost::asio::awaitable<int> aw_boost_tuple(executor_ref exec, task<int> t)
  {
    auto [ep, i] = co_await asio_spawn(exec, std::move(t))(boost::asio::as_tuple);
    BOOST_TEST(i == 42);
    co_return i;
  }

  
  ::asio::awaitable<int> aw_standalone_tuple(executor_ref exec, task<int> t)
  {
    auto [ep, i] = co_await asio_spawn(exec, std::move(t))(::asio::as_tuple);
    BOOST_TEST(i == 42);
    co_return i;
  }

  task<int> foo() {co_return 42;}
  task<int> bar() {throw 42; co_return 0;}


  void testBoost()
  {
    boost::asio::io_context ctx;

    int dispatch_count = 0;
    test_executor te{dispatch_count};
    std::exception_ptr ep;
    int i = 0;

    boost::asio::co_spawn( 
        ctx,
        aw_boost(te, foo()),
        [&](std::exception_ptr ep_, int i_) 
        {
          ep = ep; 
          i = i_;
        });

    BOOST_TEST(i == 0);
    ctx.run();

    BOOST_TEST(i == 42);
    BOOST_TEST(!ep);
    BOOST_TEST(dispatch_count == 1);
  }

  
  void testBoostTuple()
  {
    boost::asio::io_context ctx;

    int dispatch_count = 0;
    test_executor te{dispatch_count};
    std::exception_ptr ep;
    int i = 0;

    boost::asio::co_spawn( 
        ctx,
        aw_boost_tuple(te, foo()),
        [&](std::exception_ptr ep_, int i_) 
        {
          ep = ep; 
          i = i_;
        });

    BOOST_TEST(i == 0);
    ctx.run();

    BOOST_TEST(i == 42);
    BOOST_TEST(!ep);
    BOOST_TEST(dispatch_count == 1);
  }

  void testStandalone()
  {
    ::asio::io_context ctx;

    int dispatch_count = 0;
    test_executor te{dispatch_count};
    std::exception_ptr ep;
    int i = 0;

    ::asio::co_spawn( 
        ctx,
        aw_standalone(te, foo()),
        [&](std::exception_ptr ep_, int i_) 
        {
          ep = ep; 
          i = i_;
        });

    BOOST_TEST(i == 0);
    ctx.run();

    BOOST_TEST(i == 42);
    BOOST_TEST(!ep);
    BOOST_TEST(dispatch_count == 1);
  }


  void testStandaloneTuple()
  {
    ::asio::io_context ctx;

    int dispatch_count = 0;
    test_executor te{dispatch_count};
    std::exception_ptr ep;
    int i = 0;

    ::asio::co_spawn( 
        ctx,
        aw_standalone_tuple(te, foo()),
        [&](std::exception_ptr ep_, int i_) 
        {
          ep = ep; 
          i = i_;
        });

    BOOST_TEST(i == 0);
    ctx.run();

    BOOST_TEST(i == 42);
    BOOST_TEST(!ep);
    BOOST_TEST(dispatch_count == 1);
  }

  void run()
  {
    testBoost();
    testBoostTuple();

    testStandalone();
    
    testStandaloneTuple();
  }

};

TEST_SUITE(
    boost_asio_both_test,
    "boost.capy.asio.both");
#endif

} // namespace capy
} // namespace boost
