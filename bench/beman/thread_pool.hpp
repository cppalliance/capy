//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

//
// Intrusive queue and work_item base for benchmarks.
//

#ifndef BOOST_CAPY_BENCH_THREAD_POOL_HPP
#define BOOST_CAPY_BENCH_THREAD_POOL_HPP

#include <cstddef>

template <typename T>
class intrusive_queue
{
public:
    class node
    {
        friend class intrusive_queue;
        T* next_;
    };

private:
    T* head_ = nullptr;
    T* tail_ = nullptr;

public:
    intrusive_queue() = default;
    intrusive_queue(intrusive_queue const&) = delete;
    intrusive_queue& operator=(intrusive_queue const&) = delete;

    bool empty() const noexcept { return head_ == nullptr; }

    void push(T* w) noexcept
    {
        w->next_ = nullptr;
        if (tail_)
            tail_->next_ = w;
        else
            head_ = w;
        tail_ = w;
    }

    T* pop() noexcept
    {
        if (!head_)
            return nullptr;
        T* w = head_;
        head_ = head_->next_;
        if (!head_)
            tail_ = nullptr;
        return w;
    }
};

struct work_item : intrusive_queue<work_item>::node
{
    virtual void execute() noexcept = 0;
protected:
    ~work_item() = default;
};

#endif
