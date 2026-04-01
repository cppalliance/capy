//
// Copyright (c) 2026 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASIO_STANDALONE_AS_IO_AWAITABLE 
#define BOOST_CAPY_ASIO_STANDALONE_AS_IO_AWAITABLE

#include <boost/capy/asio/detail/as_io_awaitable.hpp>
#include <boost/capy/asio/detail/standalone_completion_handler.hpp>
#include <asio/async_result.hpp>

#include <optional>

template <typename... Ts>
struct asio::async_result<boost::capy::as_io_awaitable_t, void(Ts...)>
{
    template<typename Initiation, typename... Args>
    struct awaitable_t
    {
        cancellation_signal signal;
        boost::capy::detail::standalone_asio_immediate_executor_helper::completed_immediately_t ci;

        struct cb
        {
          cancellation_signal &signal;
          cb(cancellation_signal &signal) : signal(signal) {}
          void operator()() {signal.emit(cancellation_type::terminal); }
        };
        std::optional<std::stop_callback<cb>> stopper;
        
        bool await_ready() const {return false;}

        bool await_suspend(std::coroutine_handle<> h, const boost::capy::io_env * env)
        {
          stopper.emplace(env->stop_token, signal);
          boost::capy::detail::standalone_asio_coroutine_completion_handler<Ts...> ch(
            h, result_, env, 
            signal.slot(), 
            &ci);

          using ci_t = boost::capy::detail::standalone_asio_immediate_executor_helper::completed_immediately_t;
          ci = ci_t::initiating;


          std::apply(
            [&](auto ... args) 
            {
              std::move(init_)(
                std::move(ch), 
                std::move(args)...);
            }, 
            std::move(args_));

        if (ci == ci_t::initiating)
            ci =  ci_t::no;
        return ci != ci_t::yes;
        }

        std::tuple<Ts...> await_resume() {return std::move(*result_); }


        awaitable_t(Initiation init, std::tuple<Args...> args) 
              : init_(std::move(init)), args_(std::move(args)) {}

        awaitable_t(awaitable_t && rhs) noexcept 
            : init_(std::move(rhs.init_)), args_(std::move(rhs.args_)), result_(std::move(rhs.result_)) {}      private:
        Initiation init_;
        std::tuple<Args...> args_;
        std::optional<std::tuple<Ts...>> result_;
    
    };

    template <typename Initiation, typename RawCompletionToken, typename... Args>
    static auto initiate(Initiation&& initiation,
        RawCompletionToken&& token, Args&&... args)
    {
      return awaitable_t<
            std::decay_t<Initiation>, 
            std::decay_t<Args>...>(
            std::forward<Initiation>(initiation),
            std::make_tuple(std::forward<Args>(args)...));
    }
};


#endif

