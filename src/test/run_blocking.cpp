//
// Copyright (c) 2025 Vinnie Falco (vinnie.falco@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include <boost/capy/test/run_blocking.hpp>

#include <boost/capy/ex/frame_allocator.hpp>
#include <condition_variable>
#include <mutex>
#include <queue>

namespace boost {
namespace capy {
namespace test {

struct blocking_context::impl
{
    std::mutex mtx;
    std::condition_variable cv;
    std::queue<std::coroutine_handle<>> queue;
    std::exception_ptr ep;
    bool done = false;
};

blocking_context::blocking_context()
    : impl_(new impl)
{
}

blocking_context::~blocking_context()
{
    delete impl_;
}

blocking_executor
blocking_context::get_executor() noexcept
{
    return blocking_executor{this};
}

void
blocking_context::signal_done() noexcept
{
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->done = true;
    impl_->cv.notify_one();
}

void
blocking_context::signal_done(
    std::exception_ptr ep) noexcept
{
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->ep = ep;
    impl_->done = true;
    impl_->cv.notify_one();
}

void
blocking_context::run()
{
    for(;;)
    {
        std::coroutine_handle<> h;
        {
            std::unique_lock<std::mutex> lock(impl_->mtx);
            impl_->cv.wait(lock, [&] {
                return impl_->done || !impl_->queue.empty();
            });
            if(impl_->done && impl_->queue.empty())
                break;
            h = impl_->queue.front();
            impl_->queue.pop();
        }
        safe_resume(h);
    }
    if(impl_->ep)
        std::rethrow_exception(impl_->ep);
}

void
blocking_context::enqueue(
    std::coroutine_handle<> h)
{
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->queue.push(h);
    }
    impl_->cv.notify_one();
}

//----------------------------------------------------------

bool
blocking_executor::operator==(
    blocking_executor const& other) const noexcept
{
    return ctx_ == other.ctx_;
}

blocking_context&
blocking_executor::context() const noexcept
{
    return *ctx_;
}

void
blocking_executor::on_work_started() const noexcept
{
}

void
blocking_executor::on_work_finished() const noexcept
{
}

std::coroutine_handle<>
blocking_executor::dispatch(
    continuation& c) const
{
    return c.h;
}

void
blocking_executor::post(
    continuation& c) const
{
    ctx_->enqueue(c.h);
}

} // namespace test
} // namespace capy
} // namespace boost
