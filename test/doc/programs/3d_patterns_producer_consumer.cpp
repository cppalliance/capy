//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Producer/consumer program shown in pages/3.concurrency/3d.patterns.adoc.
// The page shows ThreadSafeQueue in its own block (compiled in
// snippets/3d_patterns.cpp); here it is scaffolding outside the tag.

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

template<typename T>
class ThreadSafeQueue
{
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;

public:
    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        cv_.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]{ return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }
};

// tag::full[]
ThreadSafeQueue<int> work_queue;

void producer()
{
    for (int i = 0; i < 10; ++i)
    {
        work_queue.push(i);
        std::cout << "Produced: " << i << "\n";
    }
}

void consumer()
{
    for (int i = 0; i < 10; ++i)
    {
        int item = work_queue.pop();
        std::cout << "Consumed: " << item << "\n";
    }
}

int main()
{
    std::thread prod(producer);
    std::thread cons(consumer);

    prod.join();
    cons.join();

    return 0;
}
// end::full[]
