//
// Copyright (c) 2026 Steve Gerbino
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

// Full program shown in pages/3.concurrency/3c.advanced.adoc.

// tag::full[]
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>

std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void worker()
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, []{ return ready; });  // wait until ready is true
    std::cout << "Worker proceeding!\n";
}

void signal_ready()
{
    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }
    cv.notify_one();  // wake one waiting thread
}

int main()
{
    std::thread t(worker);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    signal_ready();

    t.join();
    return 0;
}
// end::full[]
