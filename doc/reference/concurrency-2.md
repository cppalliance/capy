# Understanding Concurrency in C++

Imagine you're cooking dinner. You could boil water, wait for it to finish, then chop vegetables, wait, then sauté them—doing one thing at a time. Or you could put water on to boil, chop vegetables while waiting, and check on multiple pots simultaneously. The second approach is concurrency: managing multiple tasks that can overlap in time.

This tutorial will take you from zero knowledge to confident understanding of how concurrent programs work. By the end, you'll see your programs—and your computer—in an entirely new light.

## Part One: Why Concurrency Matters

Modern computers have multiple processor cores. A quad-core laptop can literally do four things at once. But most programs use just one core, leaving the others idle. That's like having four expert chefs in your kitchen but only letting one cook while the others watch.

Concurrency lets you use all your chefs.

Consider downloading a large file. Without concurrency, your application freezes—the user interface becomes unresponsive because your single thread of execution is busy waiting for network data. With concurrency, one thread handles the download while another keeps the interface responsive. The user can continue working, perhaps cancel the download, or start another—all while data streams in.

The benefits compound in computationally intensive work. Image processing, scientific simulations, video encoding—these tasks can be split into independent pieces. Process them simultaneously and your program finishes in a fraction of the time.

But concurrency isn't free. It introduces complexity. Multiple threads accessing the same data can corrupt it. Threads waiting on each other can freeze forever. These problems—race conditions and deadlocks—are the dragons we'll learn to slay.

First, though, we need to understand what a thread actually is.

## Part Two: Threads—Your Program's Parallel Lives

When you run a program, the operating system creates a *process* for it. This process gets its own memory space, its own resources, and at least one *thread of execution*—the main thread.

Think of a thread as a bookmark in a book of instructions. It marks where you are in the code. The processor reads the instruction at that bookmark, executes it, and moves the bookmark forward. One thread means one bookmark—your program can only be at one place in the code at a time.

But you can create additional threads. Each thread is its own bookmark, tracking its own position. Now your program can be at multiple places simultaneously. Each thread has its own *call stack*—its own record of which functions called which—but all threads share the same *heap memory*.

This sharing is both the power and the peril of threads.

Let's create our first thread.

```cpp
#include <iostream>
#include <thread>

void say_hello()
{
    std::cout << "Hello from a new thread!\n";
}

int main()
{
    std::thread t(say_hello);
    t.join();
    std::cout << "Back in the main thread.\n";
    return 0;
}
```

The `std::thread` constructor takes a function (or any *callable*—we'll see more later) and immediately starts a new thread running that function. Two bookmarks now move through your code simultaneously.

The `join()` call is crucial. It makes the main thread wait until thread `t` finishes. Without it, `main()` might return and terminate the program before `say_hello()` completes. Always join your threads before they go out of scope.

Let's see threads working in parallel.

```cpp
#include <iostream>
#include <thread>

void count_up(const char* name)
{
    for (int i = 1; i <= 5; ++i)
        std::cout << name << ": " << i << "\n";
}

int main()
{
    std::thread alice(count_up, "Alice");
    std::thread bob(count_up, "Bob");
    
    alice.join();
    bob.join();
    
    return 0;
}
```

Run this and you might see output like:

```
Alice: 1
Bob: 1
Alice: 2
Bob: 2
Alice: 3
Bob: 3
...
```

Or perhaps:

```
AliceBob: : 1
1
Alice: 2
...
```

The interleaving varies each run. Both threads race to print, and their outputs jumble together. This unpredictability is your first glimpse of concurrent programming's fundamental challenge: when threads share resources (here, `std::cout`), chaos can ensue.

## Part Three: Creating Threads—The Many Ways

You've seen functions passed to `std::thread`. But threads accept any callable object: lambda expressions, function objects (functors), and member functions.

Lambda expressions are often the clearest choice.

```cpp
#include <iostream>
#include <thread>

int main()
{
    int x = 42;
    
    std::thread t([x]() {
        std::cout << "The value is: " << x << "\n";
    });
    
    t.join();
    return 0;
}
```

The lambda captures `x` by value—it copies `x` into the lambda. This is important: by default, `std::thread` copies all arguments passed to it. Even if your function declares a reference parameter, the thread receives a copy.

To pass by reference, use `std::ref()`.

```cpp
#include <iostream>
#include <thread>

void increment(int& value)
{
    ++value;
}

int main()
{
    int counter = 0;
    
    std::thread t(increment, std::ref(counter));
    t.join();
    
    std::cout << "Counter is now: " << counter << "\n";
    return 0;
}
```

Without `std::ref()`, the thread would modify a copy, leaving `counter` unchanged. With it, the thread modifies the original.

Functors—objects with an overloaded `operator()`—work too.

```cpp
#include <iostream>
#include <thread>

class Counter
{
    int limit_;
public:
    Counter(int limit) : limit_(limit) {}
    
    void operator()() const
    {
        for (int i = 0; i < limit_; ++i)
            std::cout << i << " ";
        std::cout << "\n";
    }
};

int main()
{
    std::thread t(Counter(5));
    t.join();
    return 0;
}
```

For member functions, pass a pointer to the function and an instance.

```cpp
#include <iostream>
#include <thread>

class Greeter
{
public:
    void greet(const std::string& name)
    {
        std::cout << "Hello, " << name << "!\n";
    }
};

int main()
{
    Greeter g;
    std::thread t(&Greeter::greet, &g, "World");
    t.join();
    return 0;
}
```

The `&Greeter::greet` syntax names the member function; `&g` provides the instance to call it on.

## Part Four: Thread Lifecycle—Join, Detach, and Destruction

Every thread must be either *joined* or *detached* before its `std::thread` object is destroyed. Failing to do so calls `std::terminate()`, abruptly ending your program.

We've used `join()` extensively. It blocks the calling thread until the target thread finishes. This is how you wait for work to complete.

```cpp
std::thread t(do_work);
// ... do other things ...
t.join();  // wait for do_work to finish
```

Sometimes you want a thread to run independently, continuing even after the `std::thread` object is destroyed. That's what `detach()` does.

```cpp
std::thread t(background_task);
t.detach();  // thread runs independently
// t is now "empty"—no longer associated with a thread
```

A detached thread becomes a *daemon thread*. It runs until it finishes or the program exits. You lose all ability to wait for it or check its status. Use detachment sparingly—usually for truly fire-and-forget background work.

Before joining or detaching, you can check if a thread is *joinable*.

```cpp
std::thread t(some_function);

if (t.joinable())
{
    t.join();
}
```

A thread is joinable if it represents an actual thread of execution. After joining or detaching, or after default construction, a `std::thread` is not joinable.

## Part Five: The Danger—Race Conditions

Now we confront the central challenge of concurrent programming.

When multiple threads read the same data, all is well. But when at least one thread writes while others read or write, you have a *data race*. The result is undefined behavior—crashes, corruption, or silent errors.

Consider this innocent-looking code.

```cpp
#include <iostream>
#include <thread>

int counter = 0;

void increment_many_times()
{
    for (int i = 0; i < 100000; ++i)
        ++counter;
}

int main()
{
    std::thread t1(increment_many_times);
    std::thread t2(increment_many_times);
    
    t1.join();
    t2.join();
    
    std::cout << "Counter: " << counter << "\n";
    return 0;
}
```

Two threads, each incrementing 100,000 times. You'd expect 200,000. But run this repeatedly and you'll see different results—180,000, 195,327, maybe occasionally 200,000. Something is wrong.

The `++counter` operation looks atomic—indivisible—but it isn't. It actually consists of three steps: read the current value, add one, write the result back. Between any of these steps, the other thread might execute its own steps.

Imagine both threads read `counter` when it's 5. Both add one, getting 6. Both write 6 back. Two increments, but the counter only went up by one. This is a *lost update*, a classic race condition.

The more threads, the more opportunity for races. The faster your processor, the more instructions execute between context switches, potentially hiding the bug—until one critical day in production.

## Part Six: Mutual Exclusion—Mutexes

The solution to data races is *mutual exclusion*: ensuring that only one thread accesses shared data at a time.

A *mutex* (mutual exclusion object) is a lockable resource. Before accessing shared data, a thread *locks* the mutex. If another thread already holds the lock, the requesting thread blocks until the lock is released. This serializes access to the protected data.

```cpp
#include <iostream>
#include <thread>
#include <mutex>

int counter = 0;
std::mutex counter_mutex;

void increment_many_times()
{
    for (int i = 0; i < 100000; ++i)
    {
        counter_mutex.lock();
        ++counter;
        counter_mutex.unlock();
    }
}

int main()
{
    std::thread t1(increment_many_times);
    std::thread t2(increment_many_times);
    
    t1.join();
    t2.join();
    
    std::cout << "Counter: " << counter << "\n";
    return 0;
}
```

Now the output is always 200,000. The mutex ensures that between `lock()` and `unlock()`, only one thread executes. The increment is now effectively atomic.

But there's a problem with calling `lock()` and `unlock()` directly. If code between them throws an exception, `unlock()` never executes. The mutex stays locked forever, and any thread waiting for it blocks eternally—a *deadlock*.

## Part Seven: Lock Guards—Safety Through RAII

C++ has a powerful idiom: *RAII* (Resource Acquisition Is Initialization). The idea is simple: acquire resources in a constructor, release them in the destructor. Since destructors run even when exceptions are thrown, cleanup is guaranteed.

Lock guards apply RAII to mutexes.

```cpp
#include <iostream>
#include <thread>
#include <mutex>

int counter = 0;
std::mutex counter_mutex;

void increment_many_times()
{
    for (int i = 0; i < 100000; ++i)
    {
        std::lock_guard<std::mutex> lock(counter_mutex);
        ++counter;
        // lock is automatically released when it goes out of scope
    }
}
```

The `std::lock_guard` locks the mutex on construction and unlocks it on destruction. Even if an exception is thrown, the destructor runs and the mutex is released. This is the correct way to use mutexes.

Since C++17, `std::scoped_lock` is preferred. It works like `lock_guard` but can lock multiple mutexes simultaneously, avoiding a class of deadlock we'll discuss shortly.

```cpp
std::scoped_lock lock(counter_mutex);  // C++17
```

For more control, use `std::unique_lock`. It can be unlocked before destruction, moved to another scope, or created without immediately locking.

```cpp
std::unique_lock<std::mutex> lock(some_mutex, std::defer_lock);
// mutex not yet locked

lock.lock();  // lock when ready
// ... do work ...
lock.unlock();  // unlock early if needed
// ... do other work ...
// destructor unlocks again if still locked
```

`std::unique_lock` is more flexible but slightly more expensive than `std::lock_guard`. Use the simplest tool that does the job.

## Part Eight: The Deadlock Dragon

Mutexes solve data races but introduce a new danger: *deadlock*.

Imagine two threads and two mutexes. Thread A locks mutex 1, then tries to lock mutex 2. Thread B locks mutex 2, then tries to lock mutex 1. Each thread holds one mutex and waits for the other. Neither can proceed. The program freezes.

```cpp
std::mutex mutex1, mutex2;

void thread_a()
{
    std::lock_guard<std::mutex> lock1(mutex1);
    std::lock_guard<std::mutex> lock2(mutex2);  // blocks, waiting for B
    // ...
}

void thread_b()
{
    std::lock_guard<std::mutex> lock2(mutex2);
    std::lock_guard<std::mutex> lock1(mutex1);  // blocks, waiting for A
    // ...
}
```

If both threads run and each acquires its first mutex before the other acquires the second, deadlock occurs.

The simplest prevention: always lock mutexes in the same order. If every thread locks `mutex1` before `mutex2`, no cycle can form.

When you need to lock multiple mutexes and can't guarantee order, use `std::scoped_lock`.

```cpp
void safe_function()
{
    std::scoped_lock lock(mutex1, mutex2);  // locks both atomically
    // ...
}
```

`std::scoped_lock` uses a deadlock-avoidance algorithm internally, acquiring both mutexes without risk of circular waiting.

## Part Nine: Atomics—Lock-Free Simplicity

For simple operations on simple data, mutexes might be overkill. *Atomic types* provide lock-free thread safety for individual values.

An atomic operation completes entirely before any other thread can observe its effects. There's no intermediate state.

```cpp
#include <iostream>
#include <thread>
#include <atomic>

std::atomic<int> counter{0};

void increment_many_times()
{
    for (int i = 0; i < 100000; ++i)
        ++counter;  // atomic increment
}

int main()
{
    std::thread t1(increment_many_times);
    std::thread t2(increment_many_times);
    
    t1.join();
    t2.join();
    
    std::cout << "Counter: " << counter << "\n";
    return 0;
}
```

No mutex, no lock guard, yet the result is always 200,000. The `std::atomic<int>` ensures that increments are indivisible.

Atomics work best for single-variable operations: counters, flags, simple state. They're faster than mutexes when contention is low. But they can't protect complex operations involving multiple variables—for that, you need mutexes.

Common atomic types include `std::atomic<bool>`, `std::atomic<int>`, and `std::atomic<std::shared_ptr<T>>`. Any trivially copyable type can be made atomic.

## Part Ten: Condition Variables—Threads That Wait Intelligently

Sometimes a thread must wait for a specific condition before proceeding. You could loop, repeatedly checking:

```cpp
while (!ready)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```

This works but wastes CPU cycles and introduces latency. *Condition variables* provide efficient waiting.

A condition variable allows one thread to signal others that something has changed. Waiting threads sleep until notified, consuming no CPU.

```cpp
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
```

The worker thread calls `cv.wait()`, which atomically releases the mutex and suspends the thread. When `signal_ready()` calls `notify_one()`, the worker wakes up, reacquires the mutex, checks the condition, and proceeds.

The lambda `[]{ return ready; }` is the predicate. `wait()` won't return until this evaluates to true. This guards against *spurious wakeups*—rare events where a thread wakes without notification. Always use a predicate.

Use `notify_one()` to wake a single waiting thread, or `notify_all()` to wake them all.

## Part Eleven: Shared Locks—Readers and Writers

Consider a data structure that's read frequently but written rarely. A regular mutex serializes all access—but why block readers from each other? Multiple threads can safely read simultaneously; only writes require exclusive access.

*Shared mutexes* support this pattern.

```cpp
#include <iostream>
#include <thread>
#include <shared_mutex>
#include <vector>

std::shared_mutex rw_mutex;
std::vector<int> data;

void reader(int id)
{
    std::shared_lock<std::shared_mutex> lock(rw_mutex);  // shared access
    std::cout << "Reader " << id << " sees " << data.size() << " elements\n";
}

void writer(int value)
{
    std::unique_lock<std::shared_mutex> lock(rw_mutex);  // exclusive access
    data.push_back(value);
    std::cout << "Writer added " << value << "\n";
}
```

`std::shared_lock` acquires a *shared lock*—multiple threads can hold shared locks simultaneously. `std::unique_lock` on a shared mutex acquires an *exclusive lock*—no other locks (shared or exclusive) can be held.

While any reader holds a shared lock, writers must wait. While a writer holds an exclusive lock, everyone waits. This pattern maximizes concurrency for read-heavy workloads.

## Part Twelve: Futures and Promises—Getting Results Back

We've focused on threads as parallel workers. But how do you get results from them?

Passing references works but is clunky. C++ offers a cleaner abstraction: *futures* and *promises*.

A `std::promise` is a write-once container: a thread can set its value. A `std::future` is the corresponding read-once container: another thread can get that value. They form a one-way communication channel.

```cpp
#include <iostream>
#include <thread>
#include <future>

void compute(std::promise<int> result_promise)
{
    int answer = 6 * 7;  // expensive computation
    result_promise.set_value(answer);
}

int main()
{
    std::promise<int> promise;
    std::future<int> future = promise.get_future();
    
    std::thread t(compute, std::move(promise));
    
    std::cout << "Waiting for result...\n";
    int result = future.get();  // blocks until value is set
    std::cout << "The answer is: " << result << "\n";
    
    t.join();
    return 0;
}
```

The worker thread calls `set_value()`. The main thread calls `get()`, which blocks until the value is available. Simple, clean, safe.

A future's `get()` can only be called once. For multiple consumers, use `std::shared_future`.

## Part Thirteen: Async—The Easy Path

Creating threads manually, managing promises, joining at the end—it's mechanical. `std::async` automates it.

```cpp
#include <iostream>
#include <future>

int compute()
{
    return 6 * 7;
}

int main()
{
    std::future<int> future = std::async(compute);
    
    std::cout << "Computing...\n";
    int result = future.get();
    std::cout << "Result: " << result << "\n";
    
    return 0;
}
```

`std::async` launches the function (potentially in a new thread), returning a future. No explicit thread creation, no promise management, no join call.

By default, the system decides whether to run the function in a new thread or defer it until you call `get()`. To force a new thread:

```cpp
auto future = std::async(std::launch::async, compute);
```

To defer execution until `get()`:

```cpp
auto future = std::async(std::launch::deferred, compute);
```

For quick parallel tasks, `std::async` is often the cleanest choice.

## Part Fourteen: Thread-Local Storage

Sometimes each thread needs its own copy of a variable—not shared, not copied each call, but persistent within that thread.

Declare it `thread_local`.

```cpp
#include <iostream>
#include <thread>

thread_local int counter = 0;

void increment_and_print(const char* name)
{
    ++counter;
    std::cout << name << " counter: " << counter << "\n";
}

int main()
{
    std::thread t1([]{ 
        increment_and_print("T1");
        increment_and_print("T1");
    });
    
    std::thread t2([]{
        increment_and_print("T2");
        increment_and_print("T2");
    });
    
    t1.join();
    t2.join();
    
    return 0;
}
```

Each thread sees its own `counter`. T1 prints 1, then 2. T2 independently prints 1, then 2. No synchronization needed because the data isn't shared.

Thread-local storage is perfect for per-thread caches, random number generators, or error state.

## Part Fifteen: Practical Patterns

Let's combine what we've learned into practical patterns.

**Producer-Consumer Queue**

One or more threads produce work items; one or more threads consume them. A queue connects them.

```cpp
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
```

The producer pushes items; the consumer waits for items and processes them. The condition variable ensures the consumer sleeps efficiently when the queue is empty.

**Parallel For**

Split a loop across multiple threads.

```cpp
#include <iostream>
#include <thread>
#include <vector>

void parallel_for(int start, int end, int num_threads, 
                  std::function<void(int)> func)
{
    std::vector<std::thread> threads;
    int chunk_size = (end - start) / num_threads;
    
    for (int t = 0; t < num_threads; ++t)
    {
        int chunk_start = start + t * chunk_size;
        int chunk_end = (t == num_threads - 1) ? end : chunk_start + chunk_size;
        
        threads.emplace_back([=]{
            for (int i = chunk_start; i < chunk_end; ++i)
                func(i);
        });
    }
    
    for (auto& thread : threads)
        thread.join();
}

int main()
{
    std::mutex print_mutex;
    
    parallel_for(0, 20, 4, [&](int i){
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cout << "Processing " << i << " on thread " 
                  << std::this_thread::get_id() << "\n";
    });
    
    return 0;
}
```

The work is divided into chunks, each handled by its own thread. For CPU-bound work on large datasets, this can dramatically reduce execution time.

## Part Sixteen: What You've Learned

You began knowing nothing about concurrency. Now you understand:

- **Threads** are independent flows of execution within a process
- **Mutexes** provide mutual exclusion to prevent data races
- **Lock guards** ensure mutexes are properly released, even on exceptions
- **Atomics** offer lock-free safety for simple operations
- **Condition variables** let threads wait efficiently for events
- **Shared locks** allow multiple readers or one writer
- **Futures and promises** communicate results between threads
- **std::async** simplifies launching parallel work

You've seen the dangers—race conditions, deadlocks—and the tools to avoid them.

Concurrency is challenging. Bugs hide until the worst moment. Testing is hard because timing varies. But the rewards are substantial: responsive applications, full hardware utilization, and elegant solutions to naturally parallel problems.

The best advice: start simple. Use `std::async` when possible. Prefer immutable data. When you must share mutable state, protect it carefully. Minimize the time locks are held. Avoid nested locks when you can; when you can't, use `std::scoped_lock`.

And test. Test with many threads. Test on different machines. Test under load.

Welcome to concurrent programming. The parallel path awaits.
