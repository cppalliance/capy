//
// Copyright (c) 2025 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#ifndef BOOST_CAPY_ASYNC_RESULT_HPP
#define BOOST_CAPY_ASYNC_RESULT_HPP

#include <boost/capy/detail/config.hpp>

#ifdef BOOST_CAPY_HAS_CORO

#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <variant>

namespace boost {
namespace capy {

template<class T>
class async_result
{
public:
    struct impl_base
    {
        virtual ~impl_base() = default;
        virtual void start(std::function<void()> on_done) = 0;
        virtual T get_result() = 0;
    };

private:
    std::unique_ptr<impl_base> impl_;

public:
    explicit async_result(std::unique_ptr<impl_base> p) : impl_(std::move(p)) {}

    async_result(async_result&&) = default;
    async_result& operator=(async_result&&) = default;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
        impl_->start([h]{ h.resume(); });
    }

    T await_resume()
    {
        return impl_->get_result();
    }
};

//-----------------------------------------------------------------------------

template<class T, class DeferredOp>
struct async_result_impl : capy::async_result<T>::impl_base
{
    DeferredOp op_;
    std::variant<std::exception_ptr, T> result_;

    explicit async_result_impl(DeferredOp&& op)
        : op_(std::forward<DeferredOp>(op))
    {
    }

    void start(std::function<void()> on_done) override
    {
        std::move(op_)(
            [this, on_done = std::move(on_done)](auto&&... args) mutable
            {
                result_.template emplace<1>(T{std::forward<decltype(args)>(args)...});
                on_done();
            });
    }

    T get_result() override
    {
        if (result_.index() == 0 && std::get<0>(result_))
            std::rethrow_exception(std::get<0>(result_));
        return std::move(std::get<1>(result_));
    }
};

//-----------------------------------------------------------------------------

template<class T, class DeferredOp>
capy::async_result<T>
make_async_result(DeferredOp&& op)
{
    using impl_type = async_result_impl<T, std::decay_t<DeferredOp>>;
    return capy::async_result<T>(
        std::make_unique<impl_type>(std::forward<DeferredOp>(op)));
}

} // capy
} // boost

#endif

#endif
